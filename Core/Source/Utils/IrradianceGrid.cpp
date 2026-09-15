#include "IrradianceGrid.h"

namespace YAEngine
{
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

  bool MatchesBakedIrradianceBox(const glm::vec3& position, const glm::vec3& halfExtents,
    const glm::vec3& bakedPosition, const glm::vec3& bakedHalfExtents)
  {
    constexpr float TOLERANCE_FRACTION = 0.01f;
    const float tolerance = TOLERANCE_FRACTION * glm::max(glm::length(bakedHalfExtents), 1e-3f);
    const bool moved = glm::length(position - bakedPosition) > tolerance;
    const bool resized = glm::length(halfExtents - bakedHalfExtents) > tolerance;
    return !moved && !resized;
  }

  glm::ivec3 ComputeIrradianceSeedKey(const glm::vec3& worldPosition)
  {
    return glm::ivec3(glm::round(worldPosition / IRRADIANCE_SPACINGS[0]));
  }
}
