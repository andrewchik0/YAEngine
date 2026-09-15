#include "IrradianceBrickLayout.h"

#include "FormatText.h"

namespace YAEngine
{
  namespace
  {
    constexpr uint32_t LEVEL_COUNT = uint32_t(IRRADIANCE_SPACINGS.size());
    constexpr double KEY_METERS = double(IRRADIANCE_SPACINGS[0]);

    // Depth in meters the influence box must overlap a brick or cell by on every separating axis
    // to count as intersecting it. Bricks merely touching a box face that lies on the lattice (a
    // volume floor resting on the ground) stay out, and quaternion round-off in the box AABB
    // cannot add a layer of bricks.
    constexpr double BOX_OVERLAP_TOLERANCE = 1e-3;

    // Keeps every key, brick origin and coverage product far inside int32 range.
    constexpr double MAX_WORLD_COORDINATE = 1e6;

    // Center first, then the 8 corners, in brick node units.
    constexpr int32_t QUERY_NODE_OFFSETS[9][3] = {
      { 2, 2, 2 },
      { 0, 0, 0 }, { 4, 0, 0 }, { 0, 4, 0 }, { 4, 4, 0 },
      { 0, 0, 4 }, { 4, 0, 4 }, { 0, 4, 4 }, { 4, 4, 4 },
    };

    struct BrickId
    {
      // Origin key divided by the brick size of this level.
      glm::ivec3 index { 0 };
      uint32_t level = 0;

      bool operator==(const BrickId& other) const
      {
        return index == other.index && level == other.level;
      }
    };

    size_t MixHash(uint64_t h)
    {
      h ^= h >> 33;
      h *= 0xff51afd7ed558ccdull;
      h ^= h >> 33;
      h *= 0xc4ceb9fe1a85ec53ull;
      h ^= h >> 33;
      return size_t(h);
    }

    size_t HashCoordinates(const glm::ivec3& v, uint32_t extra)
    {
      uint64_t h = MixHash(uint64_t(uint32_t(v.x)) | (uint64_t(uint32_t(v.y)) << 32));
      return MixHash(h ^ (uint64_t(uint32_t(v.z)) | (uint64_t(extra) << 32)));
    }

    struct BrickIdHash
    {
      size_t operator()(const BrickId& id) const { return HashCoordinates(id.index, id.level); }
    };

    struct KeyHash
    {
      size_t operator()(const glm::ivec3& key) const { return HashCoordinates(key, 0); }
    };

    using BrickIdSet = std::unordered_set<BrickId, BrickIdHash>;
    using BrickIndexMap = std::unordered_map<BrickId, uint32_t, BrickIdHash>;

    int32_t FloorDiv(int32_t value, int32_t divisor)
    {
      int32_t quotient = value / divisor;
      return (value % divisor != 0 && value < 0) ? quotient - 1 : quotient;
    }

    BrickId GetAncestor(const BrickId& id, uint32_t level)
    {
      int32_t divisor = 1 << (level - id.level);
      return {
        .index = glm::ivec3(FloorDiv(id.index.x, divisor), FloorDiv(id.index.y, divisor), FloorDiv(id.index.z, divisor)),
        .level = level,
      };
    }

    glm::ivec3 GetOriginKey(const BrickId& id)
    {
      return id.index * GetIrradianceBrickSizeKeys(id.level);
    }

    BrickId GetBrickId(const IrradianceBrick& brick)
    {
      int32_t size = GetIrradianceBrickSizeKeys(brick.spacingIndex);
      return {
        .index = glm::ivec3(FloorDiv(brick.originKey.x, size), FloorDiv(brick.originKey.y, size), FloorDiv(brick.originKey.z, size)),
        .level = brick.spacingIndex,
      };
    }

    // Coarse to fine, then z, y, x.
    bool IsBrickBefore(const BrickId& a, const BrickId& b)
    {
      if (a.level != b.level) return a.level > b.level;
      if (a.index.z != b.index.z) return a.index.z < b.index.z;
      if (a.index.y != b.index.y) return a.index.y < b.index.y;
      return a.index.x < b.index.x;
    }

    // Leaves never overlap, so at most one ancestor of id (itself included) is a leaf.
    template <typename Container>
    std::optional<BrickId> FindCoveringLeaf(const Container& leaves, const BrickId& id, uint32_t maxLevel)
    {
      for (uint32_t level = id.level; level <= maxLevel; level++)
      {
        BrickId ancestor = GetAncestor(id, level);
        if (leaves.contains(ancestor))
          return ancestor;
      }
      return std::nullopt;
    }

    size_t GetBrickLocalIndex(const glm::ivec3& local)
    {
      return size_t(local.x) + size_t(local.y) * IRRADIANCE_BRICK_NODES + size_t(local.z) * IRRADIANCE_BRICK_NODES * IRRADIANCE_BRICK_NODES;
    }

    // Steps are powers of two, so the mask test is exact for negative offsets too.
    bool IsOnBrickLattice(const glm::ivec3& offset, int32_t stepKeys)
    {
      return ((offset.x | offset.y | offset.z) & (stepKeys - 1)) == 0;
    }

    // Indirection cells along one axis whose closed extent holds a key coordinate: a coordinate on
    // a cell boundary lies in the cells on both sides.
    struct AxisCells
    {
      int32_t cells[2] = {};
      int32_t count = 0;
    };

    AxisCells GetAxisCells(const IrradianceBrickLayout& layout, int32_t axis, int32_t key)
    {
      AxisCells result;
      const int64_t cellKeys = layout.indirectionCellKeys;
      const int64_t cellCount = layout.indirectionDims[axis];
      const int64_t relative = int64_t(key) - int64_t(layout.indirectionOriginKey[axis]);
      if (relative < 0 || relative > cellCount * cellKeys)
        return result;

      const int32_t lower = int32_t(relative / cellKeys);
      if (lower < cellCount)
        result.cells[result.count++] = lower;
      if (lower > 0 && lower * cellKeys == relative)
        result.cells[result.count++] = lower - 1;
      return result;
    }

    // A leaf holds a key in its closed box exactly when one of its indirection cells does, so this
    // visits every such leaf, once per cell it covers among at most 8.
    template <typename Visit>
    void ForEachLeafHolding(const IrradianceBrickLayout& layout, const AxisCells& x, const AxisCells& y, const AxisCells& z, Visit&& visit)
    {
      for (int32_t iz = 0; iz < z.count; iz++)
      {
        for (int32_t iy = 0; iy < y.count; iy++)
        {
          for (int32_t ix = 0; ix < x.count; ix++)
          {
            const uint32_t leaf = layout.indirection[layout.GetIndirectionIndex(uint32_t(x.cells[ix]), uint32_t(y.cells[iy]), uint32_t(z.cells[iz]))];
            if (leaf != IRRADIANCE_BRICK_INVALID)
              visit(leaf);
          }
        }
      }
    }

    struct SeparatingAxis
    {
      glm::dvec3 direction { 0.0 };
      double boxCenter = 0.0;
      double boxRadius = 0.0;
    };

    // The 15 SAT axes of an oriented box against world aligned boxes, projections precomputed.
    struct InfluenceBox
    {
      std::array<SeparatingAxis, 15> axes {};
      uint32_t axisCount = 0;
      glm::dvec3 aabbMin { 0.0 };
      glm::dvec3 aabbMax { 0.0 };
    };

    InfluenceBox MakeInfluenceBox(const glm::vec3& center, const glm::quat& rotation, const glm::vec3& halfExtents)
    {
      InfluenceBox box;
      glm::dmat3 basis = glm::dmat3(glm::mat3_cast(glm::normalize(rotation)));
      glm::dvec3 boxCenter(center);
      // A flat box lying exactly on a lattice plane must still overlap the bricks on both sides by
      // more than the tolerance.
      glm::dvec3 half = glm::max(glm::dvec3(halfExtents), glm::dvec3(2.0 * BOX_OVERLAP_TOLERANCE));

      auto addAxis = [&](glm::dvec3 direction)
      {
        double length = glm::length(direction);
        // A near-parallel edge pair; the face axes already cover it.
        if (length < 1e-6)
          return;
        direction /= length;

        double radius = 0.0;
        for (int32_t k = 0; k < 3; k++)
          radius += half[k] * std::abs(glm::dot(basis[k], direction));

        box.axes[box.axisCount++] = {
          .direction = direction,
          .boxCenter = glm::dot(direction, boxCenter),
          .boxRadius = radius,
        };
      };

      const glm::dvec3 worldAxes[3] = { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
      for (const auto& axis : worldAxes)
        addAxis(axis);
      for (int32_t k = 0; k < 3; k++)
        addAxis(basis[k]);
      for (const auto& axis : worldAxes)
        for (int32_t k = 0; k < 3; k++)
          addAxis(glm::cross(axis, basis[k]));

      for (int32_t i = 0; i < 3; i++)
      {
        box.aabbMin[i] = box.axes[i].boxCenter - box.axes[i].boxRadius;
        box.aabbMax[i] = box.axes[i].boxCenter + box.axes[i].boxRadius;
      }

      return box;
    }

    // Interval overlap rather than center distance, so the test is monotonic under containment: a
    // cell that passes implies every brick containing it passes too.
    bool IntersectsBox(const InfluenceBox& box, const glm::ivec3& minKey, int32_t sizeKeys)
    {
      double half = 0.5 * double(sizeKeys) * KEY_METERS;
      glm::dvec3 center = glm::dvec3(minKey) * KEY_METERS + half;

      for (uint32_t i = 0; i < box.axisCount; i++)
      {
        const SeparatingAxis& axis = box.axes[i];
        double cellCenter = glm::dot(axis.direction, center);
        double cellRadius = half * (std::abs(axis.direction.x) + std::abs(axis.direction.y) + std::abs(axis.direction.z));
        double overlap = std::min(cellCenter + cellRadius, axis.boxCenter + axis.boxRadius)
          - std::max(cellCenter - cellRadius, axis.boxCenter - axis.boxRadius);
        if (overlap <= BOX_OVERLAP_TOLERANCE)
          return false;
      }

      return true;
    }

    bool IsFinite(const glm::vec3& v)
    {
      return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    uint32_t GetSpacingIndex(float spacing)
    {
      float snapped = SnapIrradianceSpacing(spacing);
      for (uint32_t i = 0; i < LEVEL_COUNT; i++)
      {
        if (IRRADIANCE_SPACINGS[i] == snapped)
          return i;
      }
      return 0;
    }

    double GetSpacing(uint32_t level)
    {
      return double(IRRADIANCE_SPACINGS[level]);
    }

    bool Fail(IrradianceBrickLayout& layout, IrradianceBrickLayoutError error, const char* format, ...)
    {
      layout.error = error;
      va_list args;
      va_start(args, format);
      FormatText(layout.errorMessage, format, args);
      va_end(args);
      return false;
    }

    bool Reject(std::string& outFailure, const char* format, ...)
    {
      va_list args;
      va_start(args, format);
      FormatText(outFailure, format, args);
      va_end(args);
      return false;
    }

    bool BuildInto(const IrradianceBrickLayoutDesc& desc, IrradianceBrickLayout& layout)
    {
      const IrradianceBrickLayoutLimits& limits = desc.limits;
      glm::vec4 rotation(desc.rotation.x, desc.rotation.y, desc.rotation.z, desc.rotation.w);

      if (!IsFinite(desc.center) || !IsFinite(desc.halfExtents) || !std::isfinite(rotation.x)
        || !std::isfinite(rotation.y) || !std::isfinite(rotation.z) || !std::isfinite(rotation.w)
        || glm::dot(rotation, rotation) < 1e-12f)
      {
        return Fail(layout, IrradianceBrickLayoutError::InvalidDesc,
          "influence box has a non-finite center, extents or rotation");
      }

      if (glm::any(glm::lessThanEqual(desc.halfExtents, glm::vec3(0.0f))))
      {
        return Fail(layout, IrradianceBrickLayoutError::InvalidDesc,
          "influence box half extents (%g, %g, %g) must be positive",
          double(desc.halfExtents.x), double(desc.halfExtents.y), double(desc.halfExtents.z));
      }

      // SnapIrradianceSpacing would quietly turn these into 1 m.
      if (!std::isfinite(desc.minSpacing) || !std::isfinite(desc.maxSpacing))
      {
        return Fail(layout, IrradianceBrickLayoutError::InvalidDesc,
          "min spacing %g m and max spacing %g m must be finite", double(desc.minSpacing), double(desc.maxSpacing));
      }

      if (!std::isfinite(desc.refinementMargin) || desc.refinementMargin < 0.0f)
      {
        return Fail(layout, IrradianceBrickLayoutError::InvalidDesc,
          "refinement margin %g must be finite and non-negative", double(desc.refinementMargin));
      }

      uint32_t minLevel = GetSpacingIndex(desc.minSpacing);
      uint32_t maxLevel = GetSpacingIndex(desc.maxSpacing);
      if (minLevel > maxLevel)
      {
        return Fail(layout, IrradianceBrickLayoutError::InvalidDesc,
          "min spacing %g m exceeds max spacing %g m after snapping", GetSpacing(minLevel), GetSpacing(maxLevel));
      }

      if (minLevel < maxLevel && !desc.query)
      {
        return Fail(layout, IrradianceBrickLayoutError::InvalidDesc,
          "adaptive layout (%g..%g m) needs a geometry query", GetSpacing(minLevel), GetSpacing(maxLevel));
      }

      layout.center = desc.center;
      layout.rotation = glm::normalize(desc.rotation);
      layout.halfExtents = desc.halfExtents;
      layout.minSpacingIndex = minLevel;
      layout.maxSpacingIndex = maxLevel;

      InfluenceBox box = MakeInfluenceBox(layout.center, layout.rotation, layout.halfExtents);
      for (int32_t axis = 0; axis < 3; axis++)
      {
        if (std::abs(box.aabbMin[axis]) > MAX_WORLD_COORDINATE || std::abs(box.aabbMax[axis]) > MAX_WORLD_COORDINATE)
        {
          return Fail(layout, IrradianceBrickLayoutError::InvalidDesc,
            "influence box reaches beyond %g m from the origin", MAX_WORLD_COORDINATE);
        }
      }

      // Top-level coverage: the box AABB pushed out to whole bricks of maxSpacing, with the same
      // tolerance as IntersectsBox so no brick passing that test lies outside it.
      int32_t topSizeKeys = GetIrradianceBrickSizeKeys(maxLevel);
      double topSizeMeters = double(topSizeKeys) * KEY_METERS;
      glm::ivec3 topMin(0);
      glm::ivec3 topCount(1);
      for (int32_t axis = 0; axis < 3; axis++)
      {
        int32_t minIndex = int32_t(std::floor((box.aabbMin[axis] + BOX_OVERLAP_TOLERANCE) / topSizeMeters));
        int32_t maxIndex = int32_t(std::ceil((box.aabbMax[axis] - BOX_OVERLAP_TOLERANCE) / topSizeMeters));
        topMin[axis] = minIndex;
        topCount[axis] = std::max(maxIndex - minIndex, 1);
      }

      uint32_t cellsPerTop = 1u << (maxLevel - minLevel);
      glm::uvec3 dims = glm::uvec3(topCount) * cellsPerTop;
      uint64_t cellCount = uint64_t(dims.x) * uint64_t(dims.y) * uint64_t(dims.z);
      if (cellCount > limits.maxIndirectionCells)
      {
        return Fail(layout, IrradianceBrickLayoutError::TooManyIndirectionCells,
          "%llu indirection cells (%u x %u x %u) exceed the limit of %u",
          (unsigned long long)cellCount, dims.x, dims.y, dims.z, limits.maxIndirectionCells);
      }

      std::vector<BrickId> candidates;
      for (int32_t z = 0; z < topCount.z; z++)
      {
        for (int32_t y = 0; y < topCount.y; y++)
        {
          for (int32_t x = 0; x < topCount.x; x++)
          {
            BrickId id { .index = topMin + glm::ivec3(x, y, z), .level = maxLevel };
            if (!IntersectsBox(box, GetOriginKey(id), topSizeKeys))
              continue;

            candidates.push_back(id);
            if (candidates.size() > limits.maxBricks)
            {
              return Fail(layout, IrradianceBrickLayoutError::TooManyBricks,
                "more than %u bricks of %g m before refinement", limits.maxBricks, GetSpacing(maxLevel));
            }
          }
        }
      }

      // Answers stay keyed across levels: a coarse brick's corners and center are corners of its
      // children, so a finer level never asks for them again. Map nodes are address stable.
      std::unordered_map<glm::ivec3, IrradianceBrickQueryResult, KeyHash> answers;
      std::vector<const IrradianceBrickQueryResult*> candidateAnswers;
      std::vector<IrradianceBrickQueryPoint> batch;
      std::vector<IrradianceBrickQueryResult*> batchTargets;
      std::vector<IrradianceBrickQueryResult> results;
      std::vector<BrickId> leaves;
      std::vector<BrickId> next;

      for (uint32_t level = maxLevel; level > minLevel && !candidates.empty(); level--)
      {
        int32_t stepKeys = GetIrradianceBrickStepKeys(level);
        candidateAnswers.clear();
        batch.clear();
        batchTargets.clear();

        for (const BrickId& id : candidates)
        {
          glm::ivec3 origin = GetOriginKey(id);
          for (const auto& offset : QUERY_NODE_OFFSETS)
          {
            glm::ivec3 key = origin + glm::ivec3(offset[0], offset[1], offset[2]) * stepKeys;
            auto [it, inserted] = answers.try_emplace(key);
            if (inserted)
            {
              batch.push_back({ .position = GetIrradianceKeyWorldPosition(key), .seedKey = key });
              batchTargets.push_back(&it->second);
            }
            candidateAnswers.push_back(&it->second);
          }
        }

        if (!batch.empty())
        {
          results.clear();
          if (!desc.query(batch, results))
          {
            return Fail(layout, IrradianceBrickLayoutError::QueryFailed,
              "geometry query failed for %zu points of %g m bricks", batch.size(), GetSpacing(level));
          }

          if (results.size() != batch.size())
          {
            return Fail(layout, IrradianceBrickLayoutError::QueryFailed,
              "geometry query returned %zu results for %zu points", results.size(), batch.size());
          }

          for (size_t i = 0; i < batch.size(); i++)
            *batchTargets[i] = results[i];

          layout.queryBatches++;
          layout.queryPoints += uint32_t(batch.size());
        }

        double spacing = GetSpacing(level);
        double splitDistance = 2.0 * std::sqrt(3.0) * spacing + double(desc.refinementMargin) * 0.5 * spacing;
        double halfBrickSize = 0.5 * double(IRRADIANCE_BRICK_CELLS) * spacing;
        int32_t childSizeKeys = GetIrradianceBrickSizeKeys(level - 1);
        next.clear();

        for (size_t c = 0; c < candidates.size(); c++)
        {
          // std::min keeps the first argument on NaN, so a NaN distance reads as no geometry.
          float nearest = std::numeric_limits<float>::infinity();
          bool allBuried = true;
          for (size_t p = 0; p < 9; p++)
          {
            const IrradianceBrickQueryResult* answer = candidateAnswers[c * 9 + p];
            nearest = std::min(nearest, answer->nearestDistance);
            allBuried = allBuried && answer->buried;
          }

          // A buried brick that geometry touches or nearly touches still splits: lookups offset
          // outward from that surface by the normal bias land in it. Deeply buried bricks stay whole.
          const bool nearSurface = double(nearest) < halfBrickSize;
          if (double(nearest) < splitDistance && (!allBuried || nearSurface))
          {
            for (int32_t cz = 0; cz < 2; cz++)
            {
              for (int32_t cy = 0; cy < 2; cy++)
              {
                for (int32_t cx = 0; cx < 2; cx++)
                {
                  BrickId child { .index = candidates[c].index * 2 + glm::ivec3(cx, cy, cz), .level = level - 1 };
                  if (IntersectsBox(box, GetOriginKey(child), childSizeKeys))
                    next.push_back(child);
                }
              }
            }
          }
          else
          {
            leaves.push_back(candidates[c]);
          }

          if (leaves.size() + next.size() > limits.maxBricks)
          {
            return Fail(layout, IrradianceBrickLayoutError::TooManyBricks,
              "more than %u bricks while refining %g m bricks", limits.maxBricks, spacing);
          }
        }

        candidates.swap(next);
      }

      leaves.insert(leaves.end(), candidates.begin(), candidates.end());

      // 2:1 balance in one pass from fine to coarse. Splits only create leaves at least one level
      // coarser than the level being processed, and splitting never coarsens a region, so a level
      // once balanced stays balanced.
      BrickIdSet leafSet(leaves.begin(), leaves.end());
      std::vector<BrickId> levelLeaves;
      for (uint32_t level = minLevel; level + 2 <= maxLevel; level++)
      {
        levelLeaves.clear();
        for (const BrickId& id : leafSet)
        {
          if (id.level == level)
            levelLeaves.push_back(id);
        }
        std::sort(levelLeaves.begin(), levelLeaves.end(), IsBrickBefore);

        for (const BrickId& fine : levelLeaves)
        {
          for (int32_t dz = -1; dz <= 1; dz++)
          {
            for (int32_t dy = -1; dy <= 1; dy++)
            {
              for (int32_t dx = -1; dx <= 1; dx++)
              {
                if (dx == 0 && dy == 0 && dz == 0)
                  continue;

                BrickId neighbour { .index = fine.index + glm::ivec3(dx, dy, dz), .level = level };
                for (;;)
                {
                  std::optional<BrickId> coarse = FindCoveringLeaf(leafSet, neighbour, maxLevel);
                  if (!coarse || coarse->level <= level + 1)
                    break;

                  leafSet.erase(*coarse);
                  int32_t childSizeKeys = GetIrradianceBrickSizeKeys(coarse->level - 1);
                  for (int32_t cz = 0; cz < 2; cz++)
                  {
                    for (int32_t cy = 0; cy < 2; cy++)
                    {
                      for (int32_t cx = 0; cx < 2; cx++)
                      {
                        BrickId child { .index = coarse->index * 2 + glm::ivec3(cx, cy, cz), .level = coarse->level - 1 };
                        if (IntersectsBox(box, GetOriginKey(child), childSizeKeys))
                          leafSet.insert(child);
                      }
                    }
                  }

                  if (leafSet.size() > limits.maxBricks)
                  {
                    return Fail(layout, IrradianceBrickLayoutError::TooManyBricks,
                      "more than %u bricks while balancing", limits.maxBricks);
                  }
                }
              }
            }
          }
        }
      }

      std::vector<BrickId> sortedLeaves(leafSet.begin(), leafSet.end());
      std::sort(sortedLeaves.begin(), sortedLeaves.end(), IsBrickBefore);

      layout.bricks.reserve(sortedLeaves.size());
      for (const BrickId& id : sortedLeaves)
      {
        layout.bricks.push_back({ .originKey = GetOriginKey(id), .spacingIndex = id.level });
        layout.levelStats[id.level].bricks++;
      }

      layout.indirectionCellKeys = GetIrradianceBrickSizeKeys(minLevel);
      layout.indirectionOriginKey = topMin * topSizeKeys;
      layout.indirectionDims = dims;
      layout.indirection.assign(size_t(cellCount), IRRADIANCE_BRICK_INVALID);

      for (uint32_t b = 0; b < uint32_t(layout.bricks.size()); b++)
      {
        const IrradianceBrick& brick = layout.bricks[b];
        glm::uvec3 firstCell((brick.originKey - layout.indirectionOriginKey) / layout.indirectionCellKeys);
        uint32_t span = 1u << (brick.spacingIndex - minLevel);
        for (uint32_t z = 0; z < span; z++)
        {
          for (uint32_t y = 0; y < span; y++)
          {
            for (uint32_t x = 0; x < span; x++)
              layout.indirection[layout.GetIndirectionIndex(firstCell.x + x, firstCell.y + y, firstCell.z + z)] = b;
          }
        }
      }

      // Nodes and stitch records in one pass. Bricks run coarse to fine, so the first brick having a
      // key on its lattice creates the node, and that brick is the coarsest referencing it. An
      // interior key lies in its own brick alone: it is created there and never stitched. For a
      // border key the indirection yields every leaf holding it, which settles both whether an
      // earlier brick already created the node and which leaf a new node is stitched from.
      const uint32_t brickCount = uint32_t(layout.bricks.size());
      layout.brickNodeIndices.resize(size_t(brickCount) * IRRADIANCE_BRICK_NODE_COUNT);
      // Reserved to a bound instead of grown node by node: a reallocation near the node limit would
      // hold the old and the new array at once.
      layout.nodes.reserve(size_t(std::min<uint64_t>(uint64_t(brickCount) * IRRADIANCE_BRICK_NODE_COUNT,
        limits.maxUniqueNodes)));

      for (uint32_t b = 0; b < brickCount; b++)
      {
        const IrradianceBrick& brick = layout.bricks[b];
        const int32_t stepKeys = GetIrradianceBrickStepKeys(brick.spacingIndex);
        const size_t brickOffset = size_t(b) * IRRADIANCE_BRICK_NODE_COUNT;

        std::array<std::array<AxisCells, IRRADIANCE_BRICK_NODES>, 3> axisCells {};
        for (int32_t axis = 0; axis < 3; axis++)
        {
          for (int32_t i = 0; i < int32_t(IRRADIANCE_BRICK_NODES); i++)
            axisCells[axis][i] = GetAxisCells(layout, axis, brick.originKey[axis] + i * stepKeys);
        }

        for (int32_t z = 0; z < int32_t(IRRADIANCE_BRICK_NODES); z++)
        {
          for (int32_t y = 0; y < int32_t(IRRADIANCE_BRICK_NODES); y++)
          {
            for (int32_t x = 0; x < int32_t(IRRADIANCE_BRICK_NODES); x++)
            {
              const glm::ivec3 local(x, y, z);
              const glm::ivec3 key = brick.originKey + local * stepKeys;
              const bool border = glm::any(glm::equal(local, glm::ivec3(0)))
                || glm::any(glm::equal(local, glm::ivec3(int32_t(IRRADIANCE_BRICK_CELLS))));

              uint32_t owner = b;
              uint32_t source = b;
              uint32_t sourceLevel = brick.spacingIndex;
              if (border)
              {
                ForEachLeafHolding(layout, axisCells[0][x], axisCells[1][y], axisCells[2][z], [&](uint32_t leaf)
                {
                  const IrradianceBrick& other = layout.bricks[leaf];
                  if (leaf < owner && IsOnBrickLattice(key - other.originKey, GetIrradianceBrickStepKeys(other.spacingIndex)))
                    owner = leaf;

                  // Ties between same-level leaves pick the lowest index; both interpolate only the
                  // nodes of their shared border, so either gives the same value.
                  if (other.spacingIndex > sourceLevel || (other.spacingIndex == sourceLevel && leaf < source))
                  {
                    source = leaf;
                    sourceLevel = other.spacingIndex;
                  }
                });
              }

              if (owner != b)
              {
                const IrradianceBrick& ownerBrick = layout.bricks[owner];
                const glm::ivec3 ownerLocal = (key - ownerBrick.originKey) / GetIrradianceBrickStepKeys(ownerBrick.spacingIndex);
                layout.brickNodeIndices[brickOffset + GetBrickLocalIndex(local)] =
                  layout.brickNodeIndices[size_t(owner) * IRRADIANCE_BRICK_NODE_COUNT + GetBrickLocalIndex(ownerLocal)];
                continue;
              }

              if (layout.nodes.size() >= limits.maxUniqueNodes)
              {
                return Fail(layout, IrradianceBrickLayoutError::TooManyUniqueNodes,
                  "more than %u unique nodes in %zu bricks", limits.maxUniqueNodes, layout.bricks.size());
              }

              const uint32_t nodeIndex = uint32_t(layout.nodes.size());
              layout.nodes.push_back({ .key = key, .worldPosition = GetIrradianceKeyWorldPosition(key) });
              layout.levelStats[brick.spacingIndex].uniqueNodes++;
              layout.brickNodeIndices[brickOffset + GetBrickLocalIndex(local)] = nodeIndex;

              const int32_t sourceStepKeys = GetIrradianceBrickStepKeys(sourceLevel);
              const glm::ivec3 offset = key - layout.bricks[source].originKey;
              if (IsOnBrickLattice(offset, sourceStepKeys))
                continue;

              layout.stitches.push_back({
                .nodeIndex = nodeIndex,
                .sourceBrickIndex = source,
                .localCoord = glm::vec3(offset) / float(sourceStepKeys),
                .sourceSpacingIndex = sourceLevel,
              });
              layout.levelStats[brick.spacingIndex].stitchedNodes++;
            }
          }
        }
      }

      // A source brick's own stitched nodes always come from a strictly coarser leaf, so ordering
      // by source level alone resolves them first.
      std::stable_sort(layout.stitches.begin(), layout.stitches.end(),
        [](const IrradianceBrickStitch& a, const IrradianceBrickStitch& b) { return a.sourceSpacingIndex > b.sourceSpacingIndex; });

      for (uint32_t r = 0; r < uint32_t(layout.stitches.size()); r++)
        layout.nodes[layout.stitches[r].nodeIndex].stitchIndex = r;

      return true;
    }
  }

  IrradianceBrickLayout BuildIrradianceBrickLayout(const IrradianceBrickLayoutDesc& desc)
  {
    IrradianceBrickLayout layout;
    if (BuildInto(desc, layout))
      return layout;

    IrradianceBrickLayout failed;
    failed.error = layout.error;
    failed.errorMessage = std::move(layout.errorMessage);
    // The queries ran and took their time; callers report both side by side.
    failed.queryBatches = layout.queryBatches;
    failed.queryPoints = layout.queryPoints;
    return failed;
  }

  uint32_t IrradianceBrickLayout::FindNode(const glm::ivec3& key) const
  {
    uint32_t found = IRRADIANCE_BRICK_INVALID;
    if (indirectionCellKeys <= 0)
      return found;

    const AxisCells x = GetAxisCells(*this, 0, key.x);
    const AxisCells y = GetAxisCells(*this, 1, key.y);
    const AxisCells z = GetAxisCells(*this, 2, key.z);
    ForEachLeafHolding(*this, x, y, z, [&](uint32_t leaf)
    {
      const IrradianceBrick& brick = bricks[leaf];
      const int32_t stepKeys = GetIrradianceBrickStepKeys(brick.spacingIndex);
      const glm::ivec3 offset = key - brick.originKey;
      if (found == IRRADIANCE_BRICK_INVALID && IsOnBrickLattice(offset, stepKeys))
        found = brickNodeIndices[size_t(leaf) * IRRADIANCE_BRICK_NODE_COUNT + GetBrickLocalIndex(offset / stepKeys)];
    });
    return found;
  }

  bool IrradianceBrickLayout::ContainsKey(const glm::ivec3& key) const
  {
    if (indirectionCellKeys <= 0)
      return false;

    bool contained = false;
    ForEachLeafHolding(*this, GetAxisCells(*this, 0, key.x), GetAxisCells(*this, 1, key.y), GetAxisCells(*this, 2, key.z),
      [&](uint32_t) { contained = true; });
    return contained;
  }

  bool ValidateIrradianceBrickLayout(const IrradianceBrickLayout& layout, std::string& outFailure)
  {
    outFailure.clear();
    if (!layout.IsValid())
      return Reject(outFailure, "layout failed to build: %s", layout.errorMessage.c_str());

    const uint32_t minLevel = layout.minSpacingIndex;
    const uint32_t maxLevel = layout.maxSpacingIndex;
    if (minLevel > maxLevel || maxLevel >= LEVEL_COUNT)
      return Reject(outFailure, "spacing indices %u..%u are out of range", minLevel, maxLevel);

    const uint32_t brickCount = uint32_t(layout.bricks.size());
    const uint32_t nodeCount = uint32_t(layout.nodes.size());
    if (layout.brickNodeIndices.size() != size_t(brickCount) * IRRADIANCE_BRICK_NODE_COUNT)
      return Reject(outFailure, "%zu node indices for %u bricks", layout.brickNodeIndices.size(), brickCount);

    const int32_t cellKeys = GetIrradianceBrickSizeKeys(minLevel);
    const int32_t topSizeKeys = GetIrradianceBrickSizeKeys(maxLevel);
    if (layout.indirectionCellKeys != cellKeys)
      return Reject(outFailure, "indirection cell of %d keys, expected %d", layout.indirectionCellKeys, cellKeys);

    const glm::ivec3 coverageOrigin = layout.indirectionOriginKey;
    const glm::ivec3 cellDims(layout.indirectionDims);
    const int32_t cellsPerTop = 1 << (maxLevel - minLevel);
    for (int32_t axis = 0; axis < 3; axis++)
    {
      if (coverageOrigin[axis] % topSizeKeys != 0 || cellDims[axis] <= 0 || cellDims[axis] % cellsPerTop != 0)
        return Reject(outFailure, "indirection coverage is not made of whole %g m bricks", GetSpacing(maxLevel));
    }

    const size_t cellCount = size_t(cellDims.x) * size_t(cellDims.y) * size_t(cellDims.z);
    if (layout.indirection.size() != cellCount)
      return Reject(outFailure, "%zu indirection entries for %zu cells", layout.indirection.size(), cellCount);

    const glm::ivec3 coverageEnd = coverageOrigin + cellDims * cellKeys;
    const InfluenceBox box = MakeInfluenceBox(layout.center, layout.rotation, layout.halfExtents);

    BrickIndexMap brickById;
    brickById.reserve(brickCount);
    for (uint32_t b = 0; b < brickCount; b++)
    {
      const IrradianceBrick& brick = layout.bricks[b];
      if (brick.spacingIndex < minLevel || brick.spacingIndex > maxLevel)
        return Reject(outFailure, "brick %u has spacing index %u outside %u..%u", b, brick.spacingIndex, minLevel, maxLevel);

      const int32_t sizeKeys = GetIrradianceBrickSizeKeys(brick.spacingIndex);
      const BrickId id = GetBrickId(brick);
      const glm::ivec3& origin = brick.originKey;
      if (GetOriginKey(id) != origin)
        return Reject(outFailure, "brick %u origin (%d, %d, %d) is not a multiple of its size", b, origin.x, origin.y, origin.z);

      if (glm::any(glm::lessThan(origin, coverageOrigin)) || glm::any(glm::greaterThan(origin + sizeKeys, coverageEnd)))
        return Reject(outFailure, "brick %u origin (%d, %d, %d) lies outside the coverage", b, origin.x, origin.y, origin.z);

      if (b > 0 && !IsBrickBefore(GetBrickId(layout.bricks[b - 1]), id))
        return Reject(outFailure, "brick %u is out of order or a duplicate", b);

      if (!IntersectsBox(box, origin, sizeKeys))
        return Reject(outFailure, "brick %u origin (%d, %d, %d) does not intersect the influence box", b, origin.x, origin.y, origin.z);

      brickById.emplace(id, b);
    }

    // Aligned bricks overlap only when one is an ancestor of the other.
    for (uint32_t b = 0; b < brickCount; b++)
    {
      const BrickId id = GetBrickId(layout.bricks[b]);
      for (uint32_t level = id.level + 1; level <= maxLevel; level++)
      {
        auto it = brickById.find(GetAncestor(id, level));
        if (it != brickById.end())
          return Reject(outFailure, "brick %u (%g m) lies inside brick %u (%g m)", b, GetSpacing(id.level), it->second, GetSpacing(level));
      }
    }

    const glm::ivec3 coverageCellIndex = coverageOrigin / cellKeys;
    for (int32_t z = 0; z < cellDims.z; z++)
    {
      for (int32_t y = 0; y < cellDims.y; y++)
      {
        for (int32_t x = 0; x < cellDims.x; x++)
        {
          const BrickId cellId { .index = coverageCellIndex + glm::ivec3(x, y, z), .level = minLevel };
          const std::optional<BrickId> cover = FindCoveringLeaf(brickById, cellId, maxLevel);
          const uint32_t expected = cover ? brickById.at(*cover) : IRRADIANCE_BRICK_INVALID;
          const uint32_t stored = layout.indirection[layout.GetIndirectionIndex(uint32_t(x), uint32_t(y), uint32_t(z))];

          if (stored != expected)
          {
            if (expected == IRRADIANCE_BRICK_INVALID)
              return Reject(outFailure, "indirection cell (%d, %d, %d) names brick %u but no leaf covers it", x, y, z, stored);
            if (stored == IRRADIANCE_BRICK_INVALID)
              return Reject(outFailure, "indirection cell (%d, %d, %d) is empty but brick %u covers it", x, y, z, expected);
            return Reject(outFailure, "indirection cell (%d, %d, %d) names brick %u but brick %u covers it", x, y, z, stored, expected);
          }

          if (expected == IRRADIANCE_BRICK_INVALID && IntersectsBox(box, GetOriginKey(cellId), cellKeys))
            return Reject(outFailure, "indirection cell (%d, %d, %d) intersects the influence box but no leaf covers it", x, y, z);
        }
      }
    }

    for (uint32_t b = 0; b < brickCount; b++)
    {
      const BrickId id = GetBrickId(layout.bricks[b]);
      for (int32_t dz = -1; dz <= 1; dz++)
      {
        for (int32_t dy = -1; dy <= 1; dy++)
        {
          for (int32_t dx = -1; dx <= 1; dx++)
          {
            if (dx == 0 && dy == 0 && dz == 0)
              continue;

            const BrickId neighbour { .index = id.index + glm::ivec3(dx, dy, dz), .level = id.level };
            const std::optional<BrickId> cover = FindCoveringLeaf(brickById, neighbour, maxLevel);
            if (cover && cover->level > id.level + 1)
            {
              return Reject(outFailure, "2:1 balance: brick %u (%g m) touches brick %u (%g m)",
                b, GetSpacing(id.level), brickById.at(*cover), GetSpacing(cover->level));
            }
          }
        }
      }
    }

    const uint32_t stitchCount = uint32_t(layout.stitches.size());
    for (uint32_t n = 0; n < nodeCount; n++)
    {
      const IrradianceBrickNode& node = layout.nodes[n];
      glm::vec3 error = glm::abs(node.worldPosition - GetIrradianceKeyWorldPosition(node.key));
      if (ComputeIrradianceSeedKey(node.worldPosition) != node.key || glm::any(glm::greaterThan(error, glm::vec3(1e-4f))))
        return Reject(outFailure, "node %u position does not match key (%d, %d, %d)", n, node.key.x, node.key.y, node.key.z);

      if (node.stitchIndex != IRRADIANCE_BRICK_INVALID && (node.stitchIndex >= stitchCount || layout.stitches[node.stitchIndex].nodeIndex != n))
        return Reject(outFailure, "node %u names stitch record %u, which does not name it", n, node.stitchIndex);
    }

    // Together with the check above, stitch indices and records pair up one to one.
    for (uint32_t r = 0; r < stitchCount; r++)
    {
      const IrradianceBrickStitch& record = layout.stitches[r];
      if (record.nodeIndex >= nodeCount || record.sourceBrickIndex >= brickCount)
        return Reject(outFailure, "stitch record %u indices out of range", r);
      if (layout.nodes[record.nodeIndex].stitchIndex != r)
      {
        return Reject(outFailure, "stitch record %u names node %u, whose stitch index is %u",
          r, record.nodeIndex, layout.nodes[record.nodeIndex].stitchIndex);
      }

      const IrradianceBrick& source = layout.bricks[record.sourceBrickIndex];
      if (record.sourceSpacingIndex != source.spacingIndex)
        return Reject(outFailure, "stitch record %u source level %u does not match brick %u", r, record.sourceSpacingIndex, record.sourceBrickIndex);
      if (r > 0 && record.sourceSpacingIndex > layout.stitches[r - 1].sourceSpacingIndex)
        return Reject(outFailure, "stitch record %u breaks the coarse to fine order", r);

      const glm::ivec3 offset = layout.nodes[record.nodeIndex].key - source.originKey;
      const int32_t sizeKeys = GetIrradianceBrickSizeKeys(source.spacingIndex);
      const int32_t stepKeys = GetIrradianceBrickStepKeys(source.spacingIndex);
      if (glm::any(glm::lessThan(offset, glm::ivec3(0))) || glm::any(glm::greaterThan(offset, glm::ivec3(sizeKeys))))
        return Reject(outFailure, "stitch record %u: brick %u does not contain node %u", r, record.sourceBrickIndex, record.nodeIndex);
      if (offset.x % stepKeys == 0 && offset.y % stepKeys == 0 && offset.z % stepKeys == 0)
        return Reject(outFailure, "stitch record %u: node %u lies on the lattice of its source", r, record.nodeIndex);

      const glm::vec3 localError = glm::abs(glm::vec3(offset) / float(stepKeys) - record.localCoord);
      if (glm::any(glm::greaterThan(localError, glm::vec3(1e-5f))))
        return Reject(outFailure, "stitch record %u local coordinate is wrong", r);
    }

    // Brick references, the coarsest level referencing each node for the statistics, and on a
    // node's first reference the coarsest leaf holding it. That leaf is found from the brick table,
    // independently of the indirection the builder and FindNode use. With the overlap check passed
    // an interior node lies in its own brick alone, so only border nodes search.
    std::vector<uint32_t> nodeLevels(nodeCount, IRRADIANCE_BRICK_INVALID);
    for (uint32_t b = 0; b < brickCount; b++)
    {
      const IrradianceBrick& brick = layout.bricks[b];
      const int32_t stepKeys = GetIrradianceBrickStepKeys(brick.spacingIndex);
      const int32_t sizeKeys = GetIrradianceBrickSizeKeys(brick.spacingIndex);
      std::span<const uint32_t> indices = layout.GetBrickNodeIndices(b);
      for (int32_t z = 0; z < int32_t(IRRADIANCE_BRICK_NODES); z++)
      {
        for (int32_t y = 0; y < int32_t(IRRADIANCE_BRICK_NODES); y++)
        {
          for (int32_t x = 0; x < int32_t(IRRADIANCE_BRICK_NODES); x++)
          {
            const glm::ivec3 local(x, y, z);
            const uint32_t index = indices[GetBrickLocalIndex(local)];
            const glm::ivec3 key = brick.originKey + local * stepKeys;
            if (index >= nodeCount)
              return Reject(outFailure, "brick %u node (%d, %d, %d) index %u out of range", b, x, y, z, index);
            if (layout.nodes[index].key != key)
              return Reject(outFailure, "brick %u node (%d, %d, %d) resolves to the wrong key", b, x, y, z);

            const bool firstReference = nodeLevels[index] == IRRADIANCE_BRICK_INVALID;
            if (firstReference || brick.spacingIndex > nodeLevels[index])
              nodeLevels[index] = brick.spacingIndex;
            if (!firstReference)
              continue;

            const uint32_t record = layout.nodes[index].stitchIndex;
            const bool border = glm::any(glm::equal(local, glm::ivec3(0)))
              || glm::any(glm::equal(local, glm::ivec3(int32_t(IRRADIANCE_BRICK_CELLS))));
            if (!border)
            {
              if (record != IRRADIANCE_BRICK_INVALID)
                return Reject(outFailure, "node %u is interior to brick %u but stitched", index, b);
              continue;
            }

            // A leaf of this level or coarser holding the key covers one of the up to 8 bricks of
            // this level around it.
            uint32_t coarsestLevel = brick.spacingIndex;
            int32_t around[3][2] = {};
            int32_t choices[3] = {};
            for (int32_t axis = 0; axis < 3; axis++)
            {
              const int32_t lower = FloorDiv(key[axis], sizeKeys);
              around[axis][choices[axis]++] = lower;
              if (key[axis] == lower * sizeKeys)
                around[axis][choices[axis]++] = lower - 1;
            }

            for (int32_t iz = 0; iz < choices[2]; iz++)
            {
              for (int32_t iy = 0; iy < choices[1]; iy++)
              {
                for (int32_t ix = 0; ix < choices[0]; ix++)
                {
                  const BrickId id { .index = glm::ivec3(around[0][ix], around[1][iy], around[2][iz]), .level = brick.spacingIndex };
                  const std::optional<BrickId> cover = FindCoveringLeaf(brickById, id, maxLevel);
                  if (cover && cover->level > coarsestLevel)
                    coarsestLevel = cover->level;
                }
              }
            }

            const int32_t coarsestStepKeys = GetIrradianceBrickStepKeys(coarsestLevel);
            const bool onLattice = key.x % coarsestStepKeys == 0 && key.y % coarsestStepKeys == 0 && key.z % coarsestStepKeys == 0;
            if (record == IRRADIANCE_BRICK_INVALID && !onLattice)
            {
              return Reject(outFailure, "node %u (%d, %d, %d) is off the lattice of a %g m leaf containing it but is not stitched",
                index, key.x, key.y, key.z, GetSpacing(coarsestLevel));
            }
            if (record != IRRADIANCE_BRICK_INVALID && layout.stitches[record].sourceSpacingIndex != coarsestLevel)
            {
              return Reject(outFailure, "node %u is stitched from a %g m brick but a %g m leaf contains it",
                index, GetSpacing(layout.stitches[record].sourceSpacingIndex), GetSpacing(coarsestLevel));
            }
          }
        }
      }
    }

    for (uint32_t n = 0; n < nodeCount; n++)
    {
      if (nodeLevels[n] == IRRADIANCE_BRICK_INVALID)
        return Reject(outFailure, "node %u is referenced by no brick", n);

      // Also rejects two nodes sharing a key: the lookup can name only one of them.
      const glm::ivec3& key = layout.nodes[n].key;
      const uint32_t found = layout.FindNode(key);
      if (found != n)
        return Reject(outFailure, "node %u key (%d, %d, %d) is found as node %u", n, key.x, key.y, key.z, found);
    }

    // Half a step off a brick's own lattice, strictly inside it, is no node of any brick.
    for (uint32_t b = 0; b < brickCount; b++)
    {
      const IrradianceBrick& brick = layout.bricks[b];
      if (brick.spacingIndex == 0)
        continue;

      const glm::ivec3 key = brick.originKey + GetIrradianceBrickStepKeys(brick.spacingIndex) / 2;
      const uint32_t found = layout.FindNode(key);
      if (found != IRRADIANCE_BRICK_INVALID)
        return Reject(outFailure, "key (%d, %d, %d) off the lattice inside brick %u is found as node %u", key.x, key.y, key.z, b, found);
    }

    if (const uint32_t found = layout.FindNode(coverageOrigin - 1); found != IRRADIANCE_BRICK_INVALID)
      return Reject(outFailure, "a key outside the indirection coverage is found as node %u", found);

    // Every node a record interpolates with non-zero weight must be baked or resolved earlier.
    for (uint32_t r = 0; r < stitchCount; r++)
    {
      const IrradianceBrickStitch& record = layout.stitches[r];
      std::span<const uint32_t> indices = layout.GetBrickNodeIndices(record.sourceBrickIndex);

      int32_t corners[3][2] = {};
      int32_t cornerChoices[3] = {};
      for (int32_t axis = 0; axis < 3; axis++)
      {
        const float coord = record.localCoord[axis];
        const int32_t lower = std::min(int32_t(std::floor(coord)), int32_t(IRRADIANCE_BRICK_CELLS) - 1);
        const float fraction = coord - float(lower);
        if (fraction < 1.0f)
          corners[axis][cornerChoices[axis]++] = lower;
        if (fraction > 0.0f)
          corners[axis][cornerChoices[axis]++] = lower + 1;
      }

      for (int32_t iz = 0; iz < cornerChoices[2]; iz++)
      {
        for (int32_t iy = 0; iy < cornerChoices[1]; iy++)
        {
          for (int32_t ix = 0; ix < cornerChoices[0]; ix++)
          {
            const uint32_t node = indices[size_t(corners[0][ix]) + size_t(corners[1][iy]) * IRRADIANCE_BRICK_NODES
              + size_t(corners[2][iz]) * IRRADIANCE_BRICK_NODES * IRRADIANCE_BRICK_NODES];
            const uint32_t nodeRecord = layout.nodes[node].stitchIndex;
            if (nodeRecord != IRRADIANCE_BRICK_INVALID && nodeRecord >= r)
              return Reject(outFailure, "stitch record %u reads node %u before record %u resolves it", r, node, nodeRecord);
          }
        }
      }
    }

    if (minLevel == maxLevel && stitchCount != 0)
      return Reject(outFailure, "uniform layout (%g m) has %u stitch records", GetSpacing(minLevel), stitchCount);

    std::array<IrradianceBrickLevelStats, LEVEL_COUNT> expectedStats {};
    for (const IrradianceBrick& brick : layout.bricks)
      expectedStats[brick.spacingIndex].bricks++;
    for (uint32_t n = 0; n < nodeCount; n++)
    {
      expectedStats[nodeLevels[n]].uniqueNodes++;
      if (layout.nodes[n].stitchIndex != IRRADIANCE_BRICK_INVALID)
        expectedStats[nodeLevels[n]].stitchedNodes++;
    }

    for (uint32_t level = 0; level < LEVEL_COUNT; level++)
    {
      const IrradianceBrickLevelStats& stored = layout.levelStats[level];
      const IrradianceBrickLevelStats& expected = expectedStats[level];
      if (stored.bricks != expected.bricks || stored.uniqueNodes != expected.uniqueNodes || stored.stitchedNodes != expected.stitchedNodes)
      {
        return Reject(outFailure, "%g m statistics %u/%u/%u, expected %u/%u/%u bricks/nodes/stitched", GetSpacing(level),
          stored.bricks, stored.uniqueNodes, stored.stitchedNodes, expected.bricks, expected.uniqueNodes, expected.stitchedNodes);
      }
    }

    return true;
  }
}
