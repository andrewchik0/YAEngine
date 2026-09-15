#ifdef __cplusplus
#pragma once
#define vec2 glm::vec2
#define vec3 glm::vec3
namespace YAEngine {
#endif

// A texel emitting more than a white lambertian surface under unit light is bright enough
// that dropping its PBR response is invisible. Below that the surface still reads as lit,
// so it stays on the PBR path and the emissive contribution is dropped instead. The G-buffer
// pass, the path tracer and TlasBuilder's emissive light table all decide by it: an emissive
// texel is one whose luminance is strictly above it.
#define EMISSIVE_SHADING_CUTOFF 1.0

struct MaterialUniforms
{
  vec3 albedo;
  float roughness;
  vec3 emissivity;
  float specular;
  float metallic;
  int textureMask;
  int sg;
  float opacity;
  vec2 uvScale;
  float fresnelOpacity;
  float _pad1;            // keeps the struct at 64 bytes (std140 needs a multiple of 16)
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec3
#undef vec2
#endif
