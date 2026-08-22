#ifdef __cplusplus
#pragma once
#define vec4 glm::vec4
#define mat4 glm::mat4
namespace YAEngine {
#endif

#define MAX_IRRADIANCE_VOLUMES 8

// One baked irradiance volume as the shader sees it.
//
// ATLAS ADDRESSING - C++ and GLSL must agree on this exactly.
// Vulkan has no 3D texture arrays, so every volume is a sub-box of one 3D
// texture, packed along the X axis. Nodes sit on a WORLD lattice anchored at
// the world origin, so a point maps to an atlas coordinate without ever
// entering volume local space:
//
//   gridCoord  = clamp((worldPos - latticeOrigin.xyz) / latticeOrigin.w, 0, gridSize.xyz - 1)
//   atlasTexel = atlasOrigin.xyz + gridCoord + 0.5
//   atlasUVW   = atlasTexel * atlasInvSize.xyz
//
// worldToLocal and halfExtentsFade are only used for the containment test and
// the edge fade - the influence box may be rotated, the lattice never is. The
// lattice covers the world-space AABB of that box, so a point inside the box is
// always inside the lattice and the clamp never bites.
// The trailing + 0.5 lands on a texel CENTER, so gridCoord = 0 and
// gridCoord = gridSize - 1 never reach past the first and last texel of the
// sub-box - that is the half-texel padding hardware trilinear filtering needs
// to not drag in the neighbouring volume. One unused texel column is left
// between sub-boxes on top of that.
struct IrradianceVolumeInfo
{
  mat4 worldToLocal;    // world point -> volume local space, box centered at origin
  vec4 halfExtentsFade; // xyz = half-extents in meters, w = edge fade width in meters
  vec4 atlasOrigin;     // xyz = first texel of this volume inside the atlas, w unused
  vec4 gridSize;        // xyz = node counts per axis, w unused
  vec4 latticeOrigin;   // xyz = world position of node (0,0,0), w = node spacing in meters
};

struct IrradianceVolumeBuffer
{
  vec4 atlasInvSize;    // xyz = 1 / atlas size in texels, w unused
  int volumeCount;      // 0 means "skybox irradiance everywhere"
  int _pad0;
  int _pad1;
  int _pad2;
  // Sorted by ascending box volume when the buffer is filled, so the shader can
  // take the FIRST volume containing the point and a nested interior volume
  // always wins over the one enclosing it.
  IrradianceVolumeInfo volumes[MAX_IRRADIANCE_VOLUMES];
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec4
#undef mat4
#endif
