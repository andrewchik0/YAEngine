#ifdef __cplusplus
#pragma once
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
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec3
#endif
