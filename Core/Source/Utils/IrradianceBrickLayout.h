#pragma once

#include "Pch.h"
#include "IrradianceGrid.h"

namespace YAEngine
{
  // Sparse adaptive layout of an irradiance volume: 5x5x5 node bricks, dense near geometry and
  // sparse in open air. Purely geometric - no Vulkan, no Scene.
  //
  // KEYS. Every node has a global integer key on the finest lattice, key = round(world / 0.25)
  // (ComputeIrradianceSeedKey), so identical world points share one key, one seed and one node.
  // A brick of spacingIndex l (IRRADIANCE_SPACINGS[l] = 0.25 * 2^l) steps 2^l keys per node and
  // spans 4 * 2^l keys; its origin key is a multiple of that size, so it splits into exactly 8
  // bricks of l - 1. Node (x, y, z) of a brick has key origin + (x, y, z) * 2^l, stored x fastest,
  // then y, then z. Neighbouring bricks share their border nodes.

  inline constexpr uint32_t IRRADIANCE_BRICK_CELLS = 4;
  inline constexpr uint32_t IRRADIANCE_BRICK_NODES = IRRADIANCE_BRICK_CELLS + 1;
  inline constexpr uint32_t IRRADIANCE_BRICK_NODE_COUNT = IRRADIANCE_BRICK_NODES * IRRADIANCE_BRICK_NODES * IRRADIANCE_BRICK_NODES;
  inline constexpr uint32_t IRRADIANCE_BRICK_INVALID = UINT32_MAX;

  // The key arithmetic assumes spacing = finest spacing * 2^index.
  static_assert(IRRADIANCE_SPACINGS[1] == IRRADIANCE_SPACINGS[0] * 2.0f
    && IRRADIANCE_SPACINGS[2] == IRRADIANCE_SPACINGS[0] * 4.0f
    && IRRADIANCE_SPACINGS[3] == IRRADIANCE_SPACINGS[0] * 8.0f
    && IRRADIANCE_SPACINGS[4] == IRRADIANCE_SPACINGS[0] * 16.0f);

  inline int32_t GetIrradianceBrickStepKeys(uint32_t spacingIndex) { return 1 << spacingIndex; }
  inline int32_t GetIrradianceBrickSizeKeys(uint32_t spacingIndex) { return int32_t(IRRADIANCE_BRICK_CELLS) << spacingIndex; }
  inline glm::vec3 GetIrradianceKeyWorldPosition(const glm::ivec3& key) { return glm::vec3(key) * IRRADIANCE_SPACINGS[0]; }

  struct IrradianceBrickQueryPoint
  {
    glm::vec3 position { 0.0f };
    glm::ivec3 seedKey { 0 };
  };

  struct IrradianceBrickQueryResult
  {
    // +infinity when no geometry was found.
    float nearestDistance = std::numeric_limits<float>::infinity();
    bool buried = false;
  };

  // Called once per refinement level with every point that level needs, so it maps onto one GPU
  // call. Must fill outResults with exactly points.size() entries in point order; returning false
  // aborts the build. A point's answer must depend on the point alone: a point answered for a
  // coarser level is not asked again for a finer one.
  using IrradianceBrickQuery = std::function<bool(std::span<const IrradianceBrickQueryPoint> points,
    std::vector<IrradianceBrickQueryResult>& outResults)>;

  // Exceeding any limit fails the build instead of truncating it.
  struct IrradianceBrickLayoutLimits
  {
    uint32_t maxBricks = 1u << 17;
    uint32_t maxUniqueNodes = 1u << 22;
    uint32_t maxIndirectionCells = 1u << 22;
  };

  struct IrradianceBrickLayoutDesc
  {
    // Influence box, half extents positive. The lattice itself is never rotated.
    glm::vec3 center { 0.0f };
    glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 halfExtents { 1.0f };
    // Finite, snapped to IRRADIANCE_SPACINGS; min must not exceed max after snapping.
    float minSpacing = 0.5f;
    float maxSpacing = 4.0f;
    // Extra refinement distance in child cells on top of the half brick diagonal.
    float refinementMargin = 1.0f;
    IrradianceBrickLayoutLimits limits {};
    // May be empty when min == max: a uniform layout asks nothing.
    IrradianceBrickQuery query;
  };

  struct IrradianceBrick
  {
    glm::ivec3 originKey { 0 };
    uint32_t spacingIndex = 0;
  };

  struct IrradianceBrickNode
  {
    glm::ivec3 key { 0 };
    glm::vec3 worldPosition { 0.0f };
    // Index into IrradianceBrickLayout::stitches, or IRRADIANCE_BRICK_INVALID for a baked node.
    uint32_t stitchIndex = IRRADIANCE_BRICK_INVALID;
  };

  // A node whose value is never baked: it is the trilinear interpolation of the source brick's
  // nodes at localCoord, so a fine brick matches its coarser neighbour across their shared border.
  struct IrradianceBrickStitch
  {
    uint32_t nodeIndex = 0;
    uint32_t sourceBrickIndex = 0;
    // In source brick node units, 0..4 per axis.
    glm::vec3 localCoord { 0.0f };
    uint32_t sourceSpacingIndex = 0;
  };

  // A node belongs to the coarsest level referencing it, so each level's numbers are what that
  // level adds on top of the coarser ones.
  struct IrradianceBrickLevelStats
  {
    uint32_t bricks = 0;
    uint32_t uniqueNodes = 0;
    uint32_t stitchedNodes = 0;
  };

  enum class IrradianceBrickLayoutError : uint8_t
  {
    None,
    InvalidDesc,
    QueryFailed,
    TooManyBricks,
    TooManyUniqueNodes,
    TooManyIndirectionCells,
  };

  struct IrradianceBrickLayout
  {
    IrradianceBrickLayoutError error = IrradianceBrickLayoutError::None;
    // Empty unless error is set; every other output is then empty too, except queryBatches and
    // queryPoints, which count the queries that ran before the failure.
    std::string errorMessage;

    // Input box as built, for containment tests and gizmos.
    glm::vec3 center { 0.0f };
    glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 halfExtents { 0.0f };
    uint32_t minSpacingIndex = 0;
    uint32_t maxSpacingIndex = 0;

    // Leaves only, sorted coarse to fine (spacingIndex descending), then by origin z, y, x.
    std::vector<IrradianceBrick> bricks;
    // IRRADIANCE_BRICK_NODE_COUNT unique node indices per brick, in brick node order.
    std::vector<uint32_t> brickNodeIndices;
    // In order of first reference by the bricks above.
    std::vector<IrradianceBrickNode> nodes;
    // Ordered by source level coarse to fine, so every node a record interpolates from is
    // either baked or resolved by an earlier record.
    std::vector<IrradianceBrickStitch> stitches;

    // One cell per brick of minSpacing over the top-level coverage (bricks of maxSpacing over the
    // box's world AABB), x fastest, then y, then z. A cell holds the index of the leaf covering it,
    // or IRRADIANCE_BRICK_INVALID where no leaf does.
    // Every lookup, CPU or shader, must range-check the cell against indirectionDims and give an
    // out-of-range or INVALID cell weight 0: bricks overlapping the box by 1 mm or less are left
    // out, so a point inside the box within 1 mm of a face can land in either.
    glm::ivec3 indirectionOriginKey { 0 };
    glm::uvec3 indirectionDims { 0 };
    int32_t indirectionCellKeys = 0;
    std::vector<uint32_t> indirection;

    std::array<IrradianceBrickLevelStats, IRRADIANCE_SPACINGS.size()> levelStats {};
    uint32_t queryBatches = 0;
    uint32_t queryPoints = 0;

    bool IsValid() const { return error == IrradianceBrickLayoutError::None; }

    std::span<const uint32_t> GetBrickNodeIndices(uint32_t brickIndex) const
    {
      return std::span<const uint32_t>(brickNodeIndices).subspan(
        size_t(brickIndex) * IRRADIANCE_BRICK_NODE_COUNT, IRRADIANCE_BRICK_NODE_COUNT);
    }

    uint32_t GetIndirectionIndex(uint32_t x, uint32_t y, uint32_t z) const
    {
      return x + y * indirectionDims.x + z * indirectionDims.x * indirectionDims.y;
    }

    // Index of the node with this key, or IRRADIANCE_BRICK_INVALID. Constant time through the
    // indirection (at most 8 cells), so walking the node graph needs no key map: the nearest node
    // along an axis is found by probing FindNode at every multiple of 2^minSpacingIndex keys, which
    // visits every node key on the axis since all spacings are multiples of the finest. Dilating
    // invalid nodes before evaluating the stitch records walks past stitched nodes (stitchIndex),
    // taking them as neither sources nor targets since their values only exist after the records
    // run, and stops at the first probe ContainsKey rejects, so it never bridges an uncovered gap.
    uint32_t FindNode(const glm::ivec3& key) const;

    // Whether any leaf's closed box holds the key, through the indirection like FindNode.
    bool ContainsKey(const glm::ivec3& key) const;
  };

  // 1. Top level: bricks of maxSpacing intersecting the (rotated) influence box.
  // 2. Coarse to fine down to minSpacing: a brick splits when the nearest geometry at any of its
  //    center and 8 corners is closer than half its diagonal plus refinementMargin child cells,
  //    unless all 9 points are buried and none is within half the brick size of geometry.
  //    Children outside the box are dropped.
  // 3. 2:1 balance across faces, edges and corners.
  // 4. Unique nodes, indirection and stitch records, all independent of hash iteration order.
  IrradianceBrickLayout BuildIrradianceBrickLayout(const IrradianceBrickLayoutDesc& desc);

  // Checks every structural invariant the builder promises: indirection, leaf overlap, 2:1
  // balance, node keys, positions and lookup, node references, stitch sources, order and indices,
  // uniformity when min == max, and statistics. Returns false with a readable description of the
  // first failure; a failed build is always rejected.
  bool ValidateIrradianceBrickLayout(const IrradianceBrickLayout& layout, std::string& outFailure);
}
