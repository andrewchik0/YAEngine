#pragma once

#include "Pch.h"

namespace YAEngine
{
  // Allowed node spacings in meters. Powers of two so a coarse volume's nodes are always a
  // strict subset of a finer volume's, keeping shared sample points identical, not merely close.
  inline constexpr std::array<float, 5> IRRADIANCE_SPACINGS = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };

  // Snaps to the nearest allowed spacing using log2 distance (the set is geometric), not linear difference.
  float SnapIrradianceSpacing(float spacing);

  // Orientation of an influence box from a world matrix; basis is normalized so scale can't leak
  // in - halfExtents alone define the box. Baker, snapshot and editor must agree, or the previewed
  // node count won't match the bake.
  glm::quat ExtractIrradianceBoxRotation(const glm::mat4& world);

  // Whether a volume at position with halfExtents still describes the box its data was baked in, within
  // 1% of the baked box size. The baked data only lights that box, so a volume that moved or was resized
  // needs a rebake. Rotation is not compared.
  bool MatchesBakedIrradianceBox(const glm::vec3& position, const glm::vec3& halfExtents,
    const glm::vec3& bakedPosition, const glm::vec3& bakedHalfExtents);

  // Bake seed key of a probe at a world position: its integer coordinate on the finest lattice.
  // Every spacing is a multiple of the finest one, so a world point bakes the same samples
  // whichever volume, spacing or brick it is baked for.
  glm::ivec3 ComputeIrradianceSeedKey(const glm::vec3& worldPosition);
}
