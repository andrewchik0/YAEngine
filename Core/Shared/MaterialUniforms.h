#ifdef __cplusplus
#pragma once
#define vec2 glm::vec2
#define vec3 glm::vec3
namespace YAEngine {
#endif

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
