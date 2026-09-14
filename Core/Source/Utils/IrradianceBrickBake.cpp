#include "IrradianceBrickBake.h"

namespace YAEngine
{
  namespace
  {
    constexpr int32_t AXIS_DIRECTIONS[6][3] = {
      { 1, 0, 0 }, { -1, 0, 0 },
      { 0, 1, 0 }, { 0, -1, 0 },
      { 0, 0, 1 }, { 0, 0, -1 },
    };

    struct DilationNeighbour
    {
      uint32_t node = IRRADIANCE_BRICK_INVALID;
      // Inverse distance in keys
      float weight = 0.0f;
    };

    DilationNeighbour FindDilationNeighbour(const IrradianceBrickLayout& layout, const glm::ivec3& key, const glm::ivec3& direction)
    {
      const int32_t stepKeys = GetIrradianceBrickStepKeys(layout.minSpacingIndex);
      const int32_t maxDistanceKeys = GetIrradianceBrickSizeKeys(layout.maxSpacingIndex);
      for (int32_t distance = stepKeys; distance <= maxDistanceKeys; distance += stepKeys)
      {
        const glm::ivec3 probe = key + direction * distance;
        const uint32_t node = layout.FindNode(probe);
        if (node == IRRADIANCE_BRICK_INVALID)
        {
          // A node key is always covered, so only a probe holding no node needs the test.
          if (!layout.ContainsKey(probe))
            break;
          continue;
        }

        if (layout.nodes[node].stitchIndex == IRRADIANCE_BRICK_INVALID)
          return { .node = node, .weight = 1.0f / float(distance) };
      }
      return {};
    }
  }

  IrradianceBrickDilationResult DilateIrradianceBrickNodes(const IrradianceBrickLayout& layout,
    std::span<SHL1RGB> coefficients, std::span<uint8_t> filled)
  {
    assert(coefficients.size() == layout.nodes.size() && filled.size() == layout.nodes.size());

    IrradianceBrickDilationResult result;
    std::vector<uint32_t> targets;
    for (uint32_t n = 0; n < uint32_t(layout.nodes.size()); n++)
    {
      if (layout.nodes[n].stitchIndex == IRRADIANCE_BRICK_INVALID && filled[n] == 0)
        targets.push_back(n);
    }

    if (targets.empty())
      return result;

    // Found once: every wave reads the same neighbours, only their flags change.
    std::vector<DilationNeighbour> neighbours(targets.size() * 6);
    for (size_t t = 0; t < targets.size(); t++)
    {
      const glm::ivec3& key = layout.nodes[targets[t]].key;
      for (size_t d = 0; d < 6; d++)
      {
        const glm::ivec3 direction(AXIS_DIRECTIONS[d][0], AXIS_DIRECTIONS[d][1], AXIS_DIRECTIONS[d][2]);
        neighbours[t * 6 + d] = FindDilationNeighbour(layout, key, direction);
      }
    }

    std::vector<uint32_t> open(targets.size());
    for (uint32_t t = 0; t < uint32_t(open.size()); t++)
      open[t] = t;

    std::vector<std::pair<uint32_t, SHL1RGB>> wave;
    for (;;)
    {
      wave.clear();
      for (uint32_t t : open)
      {
        SHL1RGB sum {};
        float weightSum = 0.0f;
        for (size_t d = 0; d < 6; d++)
        {
          const DilationNeighbour& neighbour = neighbours[size_t(t) * 6 + d];
          if (neighbour.node != IRRADIANCE_BRICK_INVALID && filled[neighbour.node] != 0)
          {
            sum = sum + coefficients[neighbour.node] * neighbour.weight;
            weightSum += neighbour.weight;
          }
        }

        if (weightSum > 0.0f)
          wave.emplace_back(t, sum * (1.0f / weightSum));
      }

      if (wave.empty())
        break;

      for (const auto& [t, value] : wave)
      {
        coefficients[targets[t]] = value;
        filled[targets[t]] = 1;
      }

      std::erase_if(open, [&](uint32_t t) { return filled[targets[t]] != 0; });
      result.dilated += uint32_t(wave.size());
      result.waves++;
    }

    for (uint32_t t : open)
      coefficients[targets[t]] = SHL1RGB {};
    result.unreachable = uint32_t(open.size());
    return result;
  }

  void ComputeIrradianceBrickNodeFinestLevels(const IrradianceBrickLayout& layout, std::span<uint8_t> outLevels)
  {
    assert(outLevels.size() == layout.nodes.size());

    std::fill(outLevels.begin(), outLevels.end(), UINT8_MAX);
    for (uint32_t b = 0; b < uint32_t(layout.bricks.size()); b++)
    {
      const uint8_t level = uint8_t(layout.bricks[b].spacingIndex);
      for (uint32_t node : layout.GetBrickNodeIndices(b))
        outLevels[node] = std::min(outLevels[node], level);
    }
  }

  void StitchIrradianceBrickNodes(const IrradianceBrickLayout& layout, std::span<SHL1RGB> coefficients,
    std::span<uint8_t> validity)
  {
    assert(coefficients.size() == layout.nodes.size() && validity.size() == layout.nodes.size());

    const int32_t lastCell = int32_t(IRRADIANCE_BRICK_CELLS) - 1;
    for (const IrradianceBrickStitch& record : layout.stitches)
    {
      const std::span<const uint32_t> indices = layout.GetBrickNodeIndices(record.sourceBrickIndex);

      // A coordinate on the far face lies in the last cell at weight 1.
      glm::ivec3 lower(0);
      glm::vec3 fraction(0.0f);
      for (int32_t axis = 0; axis < 3; axis++)
      {
        lower[axis] = std::min(int32_t(std::floor(record.localCoord[axis])), lastCell);
        fraction[axis] = record.localCoord[axis] - float(lower[axis]);
      }

      SHL1RGB value {};
      bool valid = false;
      for (int32_t dz = 0; dz < 2; dz++)
      {
        for (int32_t dy = 0; dy < 2; dy++)
        {
          for (int32_t dx = 0; dx < 2; dx++)
          {
            const float weight = (dx != 0 ? fraction.x : 1.0f - fraction.x)
              * (dy != 0 ? fraction.y : 1.0f - fraction.y)
              * (dz != 0 ? fraction.z : 1.0f - fraction.z);
            // A zero weight corner may be a stitched node a later record resolves.
            if (weight == 0.0f)
              continue;

            const size_t local = size_t(lower.x + dx) + size_t(lower.y + dy) * IRRADIANCE_BRICK_NODES
              + size_t(lower.z + dz) * IRRADIANCE_BRICK_NODES * IRRADIANCE_BRICK_NODES;
            value = value + coefficients[indices[local]] * weight;
            valid = valid || validity[indices[local]] != 0;
          }
        }
      }

      coefficients[record.nodeIndex] = value;
      validity[record.nodeIndex] = valid ? 1 : 0;
    }
  }
}
