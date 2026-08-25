#pragma once

#include "Pch.h"
#include "SphericalHarmonics.h"

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

  // World-space AABB half-extents of the rotated box; this is also what the editor checks for overlapping volumes.
  glm::vec3 ComputeRotatedBoxAabbHalfExtents(const glm::quat& rotation, const glm::vec3& halfExtents);

  // Node layout of one irradiance volume (purely geometric, no Vulkan/Scene). The lattice is world
  // axis aligned and anchored at the origin, so a rotated influence box only selects which lattice
  // nodes get baked (some outside the box, so trilinear stays defined at the rotated faces). Nodes
  // are ordered x, then y, then z, matching IrradianceVolumeFile and the 3D texture upload.
  struct IrradianceGridLayout
  {
    glm::uvec3 nodeCounts { 2 };
    // Exact spacing in meters, one of IRRADIANCE_SPACINGS. Never recomputed:
    // the box is snapped to the lattice instead.
    float spacing { 1.0f };
    // Half-extents of the ROTATED influence box. Used by the containment test and
    // the edge fade only - the lattice does not depend on them.
    glm::vec3 halfExtents { 1.0f };
    // World position of node (0, 0, 0). A multiple of spacing on every axis.
    glm::vec3 latticeOrigin { 0.0f };

    uint32_t GetNodeCount() const
    {
      return nodeCounts.x * nodeCounts.y * nodeCounts.z;
    }

    // Six offscreen renders per node - this, not memory, is the bake budget.
    uint32_t GetFaceRenderCount() const
    {
      return GetNodeCount() * 6;
    }

    uint32_t GetNodeIndex(uint32_t x, uint32_t y, uint32_t z) const
    {
      return x + y * nodeCounts.x + z * nodeCounts.x * nodeCounts.y;
    }

    // Node center in world space. No box center, no rotation - that is the whole
    // point of the world lattice.
    glm::vec3 GetWorldPosition(uint32_t x, uint32_t y, uint32_t z) const
    {
      return latticeOrigin + glm::vec3(float(x), float(y), float(z)) * spacing;
    }
  };

  // Snaps spacing to IRRADIANCE_SPACINGS, pushes both AABB corners out to lattice multiples so the
  // box spans whole cells, and clamps node count per axis to at least 2 (trilinear filtering needs
  // a node on both sides of every axis).
  IrradianceGridLayout ComputeIrradianceGridLayout(const glm::vec3& center,
    const glm::quat& rotation, const glm::vec3& halfExtents, float spacing);

  // Fills invalid nodes with the nearest valid node's coefficients (multi-source BFS, 6-connectivity),
  // so every lattice node is safe for hardware trilinear filtering without manual validity weighting.
  // Validity array is left untouched (travels into the asset for the editor). Returns false if no
  // node was valid at all.
  bool FloodFillIrradianceNodes(const IrradianceGridLayout& layout,
    std::vector<SHL1RGB>& coefficients, const std::vector<uint8_t>& validity);
}
