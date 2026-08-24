#include "MeshSimplifier.h"

#include <meshoptimizer.h>

namespace YAEngine
{
  MeshLodLevels MeshSimplifier::Build(const glm::vec3* positions, size_t vertexCount,
    const std::vector<uint32_t>& indices)
  {
    MeshLodLevels result;

    if (positions == nullptr || vertexCount == 0 || indices.empty() || indices.size() % 3 != 0)
      return result;

    if (indices.size() / 3 < MIN_TRIANGLES)
      return result;

    result.levels.resize(LOD_COUNT - 1);

    std::vector<uint32_t> scratch(indices.size());
    size_t previousIndexCount = indices.size();

    for (uint32_t level = 0; level < LOD_COUNT - 1; level++)
    {
      // Every level simplifies the source mesh rather than the level above it.
      // Chaining would accumulate error twice for the same triangle budget.
      size_t targetIndexCount = size_t(float(indices.size()) * LEVEL_RATIOS[level]) / 3 * 3;
      if (targetIndexCount < 3)
        break;

      float resultError = 0.0f;
      size_t producedCount = meshopt_simplify(
        scratch.data(), indices.data(), indices.size(),
        &positions->x, vertexCount, sizeof(glm::vec3),
        targetIndexCount, TARGET_ERROR,
        // Open borders are where two meshes of the same object meet. Letting them
        // move opens cracks the shadow map renders straight through.
        meshopt_SimplifyLockBorder,
        &resultError);

      // Topology can stop the simplifier well short of the target; when what it
      // produced is close to the previous level, the level is not worth storing.
      if (producedCount == 0 || float(producedCount) >= float(previousIndexCount) * WORTHWHILE_RATIO)
        continue;

      result.levels[level].assign(scratch.begin(), scratch.begin() + producedCount);
      previousIndexCount = producedCount;
    }

    return result;
  }
}
