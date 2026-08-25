#include "MeshSimplifier.h"

#include <meshoptimizer.h>

#include "Utils/Log.h"

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

    // The factor meshopt uses to convert between its relative metric and absolute
    // units, which is the largest extent of the mesh.
    float extent = meshopt_simplifyScale(&positions->x, vertexCount, sizeof(glm::vec3));

    std::vector<uint32_t> scratch(indices.size());
    size_t previousIndexCount = indices.size();

    for (uint32_t level = 0; level < LOD_COUNT - 1; level++)
    {
      // Every level simplifies the source mesh rather than the level above it.
      // Chaining would accumulate error twice for the same triangle budget.
      size_t targetIndexCount = size_t(float(indices.size()) * LEVEL_RATIOS[level]) / 3 * 3;
      if (targetIndexCount < 3)
        break;

      float budget = std::min(LEVEL_MAX_ERROR[level], TARGET_ERROR * extent);

      float resultError = 0.0f;
      size_t producedCount = meshopt_simplify(
        scratch.data(), indices.data(), indices.size(),
        &positions->x, vertexCount, sizeof(glm::vec3),
        targetIndexCount, budget,
        // Open borders are where two meshes of the same object meet. Letting them
        // move opens cracks the shadow map renders straight through.
        meshopt_SimplifyLockBorder | meshopt_SimplifyErrorAbsolute,
        &resultError);

      // Topology can stop the simplifier well short of the target; when what it
      // produced is close to the previous level, the level is not worth storing.
      if (producedCount == 0 || float(producedCount) >= float(previousIndexCount) * WORTHWHILE_RATIO)
        continue;

      // The budget is a request, not a guarantee, so the level that actually came
      // back is measured against it rather than assumed to obey it.
      if (resultError > budget)
      {
        s_RejectedLevels++;
        if (!s_RejectionReported)
        {
          s_RejectionReported = true;
          YA_LOG_WARN("Render",
            "Shadow LOD level %u dropped: deformation %.3f cm exceeds the %.3f cm budget on a mesh %.1f units across",
            level + 1, resultError * 100.0f, budget * 100.0f, extent);
        }
        continue;
      }

      if (resultError > s_MaxKeptError)
      {
        s_MaxKeptError = resultError;
        s_MaxKeptErrorExtent = extent;
      }

      result.levels[level].assign(scratch.begin(), scratch.begin() + producedCount);
      previousIndexCount = producedCount;
    }

    return result;
  }

  void MeshSimplifier::LogWorstError()
  {
    YA_LOG_INFO("Render",
      "Shadow LOD deformation: worst kept %.3f cm on a mesh %.1f units across, %u levels dropped over budget",
      s_MaxKeptError * 100.0f, s_MaxKeptErrorExtent, s_RejectedLevels);
  }
}
