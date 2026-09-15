#ifdef __cplusplus
#pragma once
#define vec4 glm::vec4
#define ivec4 glm::ivec4
#define mat4 glm::mat4
namespace YAEngine {
#endif

#define MAX_IRRADIANCE_VOLUMES 8

// Brick pool and indirection atlas encoding, see the addressing convention below.
#define IRRADIANCE_POOL_BRICK_TEXELS   5
#define IRRADIANCE_POOL_SLOT_MASK      0x0FFFFFFFu
#define IRRADIANCE_POOL_SPACING_SHIFT  28
#define IRRADIANCE_INDIRECTION_INVALID 0xFFFFFFFFu
// Spacing of spacing index 0 in meters; index l is this times 2^l.
#define IRRADIANCE_FINEST_SPACING      0.25

// Brick color per spacing index, finest to coarsest, as vec4 constructor arguments: the placement
// brick gizmo draws them with their alpha, the volume level debug view takes their rgb.
#define IRRADIANCE_LEVEL_COLOR_0 1.0, 0.3, 0.3, 0.8
#define IRRADIANCE_LEVEL_COLOR_1 1.0, 0.85, 0.2, 0.75
#define IRRADIANCE_LEVEL_COLOR_2 0.35, 0.95, 0.35, 0.7
#define IRRADIANCE_LEVEL_COLOR_3 0.3, 0.85, 1.0, 0.65
#define IRRADIANCE_LEVEL_COLOR_4 0.5, 0.55, 1.0, 0.6

// One baked irradiance volume as the shader sees it.
//
// BRICK ADDRESSING - IrradianceVolumeStorage and computeDiffuseIBL must agree on this exactly.
// A volume is sparse bricks of 5x5x5 nodes (Utils/IrradianceBrickLayout.h) on the WORLD lattice,
// so a point is looked up without ever entering volume local space.
//
// Pool: three RGBA16F 3D textures, one per color channel with (L0, L1x, L1y, L1z) per texel, all a
// grid of slots of 5x5x5 texels shared by every volume. A brick's nodes fill its slot x fastest,
// then y, then z. Slot s sits at slot coordinate
//   slotCoord = (s % poolSlotsX, (s / poolSlotsX) % poolSlotsY, s / (poolSlotsX * poolSlotsY))
//
// Indirection: one R32UI 3D texture holding every volume's cell grid, one cell per brick of the
// volume's finest spacing, the grids placed side by side along one axis that the upload picks.
// A texel is slot | spacingIndex << 28, or IRRADIANCE_INDIRECTION_INVALID where no brick covers
// the cell.
//
//   cell        = ivec3(floor((worldPos - indirectionOrigin.xyz) / indirectionOrigin.w))
//   entry       = texelFetch(indirection, indirectionAtlas.xyz + cell, 0).r
//   slot        = entry & IRRADIANCE_POOL_SLOT_MASK
//   level       = entry >> IRRADIANCE_POOL_SPACING_SHIFT
//   spacing     = IRRADIANCE_FINEST_SPACING * 2^level
//   ratio       = 2^(level - indirectionDims.w)
//   brickOrigin = indirectionOrigin.xyz + vec3((cell / ratio) * ratio) * indirectionOrigin.w
//   local       = clamp((worldPos - brickOrigin) / spacing, 0, 4)
//   poolUVW     = (slotCoord * 5 + local + 0.5) * poolInvSize.xyz
//
// A cell outside [0, indirectionDims.xyz) or holding IRRADIANCE_INDIRECTION_INVALID gives the
// volume weight 0: bricks overlapping the box by 1 mm or less are left out of the layout, so a
// point inside the box within 1 mm of a face can land in either.
// The integer (cell / ratio) * ratio is exact because the grid origin is a multiple of every
// brick size in the volume. Clamping local to 0..4 plus the 0.5 keeps the sample between the
// first and last texel centers of the slot, so trilinear filtering never reaches a neighbour.
// worldToLocal and halfExtentsFade only drive the containment test and the edge fade - the
// influence box may be rotated, the lattice never is. The fade width is the edge fade the volume was
// baked with (IrradianceVolumeComponent::edgeFade), one value per volume: the spacing of the brick
// a point lands in would make the blend jump where levels meet along a face.
struct IrradianceVolumeInfo
{
  mat4 worldToLocal;       // world point -> volume local space, box centered at origin
  vec4 halfExtentsFade;    // xyz = half extents in meters, w = edge fade width in meters, from the volume file
  vec4 indirectionOrigin;  // xyz = world position of the minimum corner of cell (0,0,0), w = cell size in meters
  ivec4 indirectionAtlas;  // xyz = texel of cell (0,0,0) in the indirection atlas, w unused
  ivec4 indirectionDims;   // xyz = cells per axis, w = spacing index of the finest bricks
};

struct IrradianceVolumeBuffer
{
  vec4 poolInvSize;        // xyz = 1 / brick pool size in texels, w unused
  int volumeCount;         // 0 means "skybox irradiance everywhere"
  int poolSlotsX;
  int poolSlotsY;
  int _pad0;
  // Sorted by ascending box volume when the buffer is filled, so the shader can
  // take the FIRST volume containing the point and a nested interior volume
  // always wins over the one enclosing it.
  IrradianceVolumeInfo volumes[MAX_IRRADIANCE_VOLUMES];
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec4
#undef ivec4
#undef mat4
#endif
