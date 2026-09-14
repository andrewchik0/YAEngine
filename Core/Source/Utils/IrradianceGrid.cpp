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

  glm::ivec3 ComputeIrradianceSeedKey(const glm::vec3& worldPosition)
  {
    return glm::ivec3(glm::round(worldPosition / IRRADIANCE_SPACINGS[0]));
  }
}
