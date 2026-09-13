#ifdef __cplusplus
#pragma once
#define uint uint32_t
#define vec2 glm::vec2
#define vec3 glm::vec3
namespace YAEngine {
#endif

// RayTracingMaterialRecord::textureMask. The numbering is MaterialUniforms::textureMask's,
// deliberately: a hit shader and the G-buffer shader read the same material and there is
// nothing to gain from two encodings of "this slot has a texture". Bit 7 (cubemap IBL) has
// no meaning here and is never set.
#define RT_MATERIAL_BASE_COLOR   0x001u
#define RT_MATERIAL_METALLIC     0x002u
#define RT_MATERIAL_ROUGHNESS    0x004u
#define RT_MATERIAL_SPECULAR     0x008u
#define RT_MATERIAL_EMISSIVE_MAP 0x010u
#define RT_MATERIAL_NORMAL       0x020u
#define RT_MATERIAL_COMBINED     0x100u
#define RT_MATERIAL_TWO_CHANNEL_NORMAL 0x200u
#define RT_MATERIAL_EMISSIVE_SHADING   0x400u

// One record per MaterialManager slot, at the slot's own index - which is what a
// RayTracingInstanceRecord::materialIndex names. The array is dense in index space, so a
// slot no live material occupies holds a zeroed record; nothing indexes one, because an
// instance is only written for a material that resolved.
//
// The *Index fields are slots in the bindless texture table
// (BindlessTextureRegistry). Index 0 is the permanently bound 1x1 white fallback, so a
// material whose map is unset, still loading or already destroyed samples white rather
// than an unwritten descriptor. Sample one only after its textureMask bit says it is
// there - PARTIALLY_BOUND makes an unwritten slot safe only while nothing reads it.
//
// std430: vec3 aligns to 16 and vec2 to 8, which is what puts uvScale at offset 40 and
// keeps the whole record at 80 bytes on both sides.
struct RayTracingMaterialRecord
{
  vec3 albedo;
  float roughness;
  vec3 emissivity;      // already multiplied by emissiveIntensity, as the raster path does
  float specular;
  float metallic;
  float opacity;
  vec2 uvScale;
  uint textureMask;
  uint baseColorIndex;
  uint metallicIndex;
  uint roughnessIndex;
  uint specularIndex;
  uint emissiveIndex;
  uint normalIndex;
  uint _pad0;           // keeps the struct at 80 bytes, its std430 stride
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec3
#undef vec2
#undef uint
#endif
