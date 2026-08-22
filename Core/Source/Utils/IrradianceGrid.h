#pragma once

#include "Pch.h"
#include "SphericalHarmonics.h"

namespace YAEngine
{
  // Allowed node spacings in meters. Powers of two on purpose: with a lattice
  // anchored at the world origin, the nodes of a coarse volume are a strict
  // subset of the nodes of a finer one, so a shared node is literally the same
  // sample point in both and the baked values are identical, not merely close.
  inline constexpr std::array<float, 5> IRRADIANCE_SPACINGS = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };

  // Snaps an arbitrary spacing to the nearest allowed one. The metric is the
  // ratio (distance in log2 space), not the linear difference - the set is
  // geometric, so 3.0 is closer to 4.0 than to 2.0, and there are no ties.
  float SnapIrradianceSpacing(float spacing);

  // Orientation of an influence box taken from an entity world matrix, with the
  // basis normalized so transform scale cannot leak into the box - halfExtents
  // alone define it. The baker, the snapshot and the editor must agree on this,
  // otherwise the node count shown before a bake is not the one that gets baked.
  glm::quat ExtractIrradianceBoxRotation(const glm::mat4& world);

  // World-space AABB half-extents of a box rotated by `rotation`. The sampling
  // lattice covers this AABB, so it is also the box the editor compares against
  // when it looks for overlapping volumes.
  glm::vec3 ComputeRotatedBoxAabbHalfExtents(const glm::quat& rotation, const glm::vec3& halfExtents);

  // Node layout of one irradiance volume. Purely geometric - no Vulkan, no Scene.
  //
  // The sampling lattice is world axis aligned and anchored at the world origin:
  // every node sits at a multiple of `spacing` on every axis. The influence box
  // may still be rotated - the lattice then covers the world-space AABB of the
  // rotated box, and nodes that fall outside the box are still baked so trilinear
  // interpolation stays well defined right up to the rotated faces.
  //
  // Nodes are ordered x first, then y, then z, matching IrradianceVolumeFile and
  // the 3D texture upload, so a node array is never reshuffled.
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

  // Lays the world lattice over the world-space AABB of the rotated box: the
  // spacing is snapped to IRRADIANCE_SPACINGS and then kept exact, and both AABB
  // corners are pushed out to lattice multiples so the box spans whole cells.
  // Node count per axis is max(2, cells + 1). Two is the hard minimum because
  // hardware trilinear filtering needs a node on both sides of every axis.
  IrradianceGridLayout ComputeIrradianceGridLayout(const glm::vec3& center,
    const glm::quat& rotation, const glm::vec3& halfExtents, float spacing);

  // Replaces every invalid node with the coefficients of the nearest valid one
  // (multi-source BFS over 6-connectivity). This is what lets the shader trust
  // hardware trilinear filtering: an interpolation cell can never contain a node
  // that was never captured, so three texture fetches are enough and no manual
  // 8-tap validity weighting is needed.
  //
  // The validity array is left untouched - it travels into the asset as is so the
  // editor can still show which nodes were rejected.
  // Returns false when no node was valid at all; coefficients are then all zero.
  bool FloodFillIrradianceNodes(const IrradianceGridLayout& layout,
    std::vector<SHL1RGB>& coefficients, const std::vector<uint8_t>& validity);
}
