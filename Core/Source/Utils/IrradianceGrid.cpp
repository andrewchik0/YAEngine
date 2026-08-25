#include "IrradianceGrid.h"

namespace YAEngine
{
  namespace
  {
    // Tolerance in lattice units so float rounding doesn't add an extra cell to a corner already on the lattice.
    constexpr float LATTICE_SNAP_EPSILON = 1e-4f;
  }

  float SnapIrradianceSpacing(float spacing)
  {
    // Negated form also catches NaN; infinity needs an explicit check since log2(inf/candidate)
    // is inf for every candidate, which would wrongly pick the finest spacing.
    if (!(spacing > 0.0f) || !std::isfinite(spacing))
      return IRRADIANCE_SPACINGS[2];

    float best = IRRADIANCE_SPACINGS[0];
    float bestDistance = std::numeric_limits<float>::max();
    for (float candidate : IRRADIANCE_SPACINGS)
    {
      float distance = std::abs(std::log2(spacing / candidate));
      if (distance < bestDistance)
      {
        bestDistance = distance;
        best = candidate;
      }
    }

    return best;
  }

  glm::quat ExtractIrradianceBoxRotation(const glm::mat4& world)
  {
    glm::mat3 basis = glm::mat3(world);
    for (int32_t axis = 0; axis < 3; axis++)
    {
      float len = glm::length(basis[axis]);
      if (len > 1e-6f)
        basis[axis] /= len;
      else
        basis[axis] = glm::vec3(axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f);
    }

    return glm::normalize(glm::quat_cast(basis));
  }

  glm::vec3 ComputeRotatedBoxAabbHalfExtents(const glm::quat& rotation, const glm::vec3& halfExtents)
  {
    // The extent along world axis i is the sum of the box half-extents projected
    // onto it, hence the absolute value of the rotation matrix.
    glm::mat3 basis = glm::mat3_cast(glm::normalize(rotation));
    glm::vec3 clamped = glm::max(halfExtents, glm::vec3(0.0f));

    glm::vec3 aabbHalf(0.0f);
    for (uint32_t axis = 0; axis < 3; axis++)
    {
      aabbHalf[axis] = std::abs(basis[0][axis]) * clamped.x
        + std::abs(basis[1][axis]) * clamped.y
        + std::abs(basis[2][axis]) * clamped.z;
    }

    return aabbHalf;
  }

  IrradianceGridLayout ComputeIrradianceGridLayout(const glm::vec3& center,
    const glm::quat& rotation, const glm::vec3& halfExtents, float spacing)
  {
    IrradianceGridLayout layout;
    layout.halfExtents = glm::max(halfExtents, glm::vec3(0.0f));
    layout.spacing = SnapIrradianceSpacing(spacing);

    glm::vec3 aabbHalf = ComputeRotatedBoxAabbHalfExtents(rotation, layout.halfExtents);

    for (uint32_t axis = 0; axis < 3; axis++)
    {
      float minCoord = (center[axis] - aabbHalf[axis]) / layout.spacing;
      float maxCoord = (center[axis] + aabbHalf[axis]) / layout.spacing;

      int32_t minIndex = int32_t(std::floor(minCoord + LATTICE_SNAP_EPSILON));
      int32_t maxIndex = int32_t(std::ceil(maxCoord - LATTICE_SNAP_EPSILON));

      // Hardware trilinear filtering needs a node on both sides of every axis
      if (maxIndex <= minIndex)
        maxIndex = minIndex + 1;

      layout.nodeCounts[axis] = uint32_t(maxIndex - minIndex) + 1;
      layout.latticeOrigin[axis] = float(minIndex) * layout.spacing;
    }

    return layout;
  }

  bool FloodFillIrradianceNodes(const IrradianceGridLayout& layout,
    std::vector<SHL1RGB>& coefficients, const std::vector<uint8_t>& validity)
  {
    uint32_t nodeCount = layout.GetNodeCount();
    if (coefficients.size() != nodeCount || validity.size() != nodeCount)
      return false;

    std::vector<uint8_t> filled(nodeCount, 0);
    std::deque<uint32_t> queue;

    for (uint32_t i = 0; i < nodeCount; i++)
    {
      if (validity[i] == 0) continue;
      filled[i] = 1;
      queue.push_back(i);
    }

    if (queue.empty())
    {
      std::fill(coefficients.begin(), coefficients.end(), SHL1RGB {});
      return false;
    }

    const glm::ivec3 offsets[6] = {
      { 1, 0, 0 }, { -1, 0, 0 },
      { 0, 1, 0 }, { 0, -1, 0 },
      { 0, 0, 1 }, { 0, 0, -1 },
    };

    glm::ivec3 counts(layout.nodeCounts);

    while (!queue.empty())
    {
      uint32_t index = queue.front();
      queue.pop_front();

      uint32_t planeSize = layout.nodeCounts.x * layout.nodeCounts.y;
      glm::ivec3 node(
        int32_t(index % layout.nodeCounts.x),
        int32_t((index / layout.nodeCounts.x) % layout.nodeCounts.y),
        int32_t(index / planeSize));

      for (const auto& offset : offsets)
      {
        glm::ivec3 neighbour = node + offset;
        if (neighbour.x < 0 || neighbour.y < 0 || neighbour.z < 0) continue;
        if (neighbour.x >= counts.x || neighbour.y >= counts.y || neighbour.z >= counts.z) continue;

        uint32_t neighbourIndex = layout.GetNodeIndex(
          uint32_t(neighbour.x), uint32_t(neighbour.y), uint32_t(neighbour.z));
        if (filled[neighbourIndex]) continue;

        filled[neighbourIndex] = 1;
        coefficients[neighbourIndex] = coefficients[index];
        queue.push_back(neighbourIndex);
      }
    }

    return true;
  }
}
