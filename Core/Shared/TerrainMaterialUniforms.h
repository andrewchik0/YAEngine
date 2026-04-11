#ifdef __cplusplus
#pragma once
#define vec3 glm::vec3
namespace YAEngine {
#endif

struct TerrainMaterialUniforms
{
  vec3 layer0Albedo;
  float layer0Roughness;
  vec3 layer1Albedo;
  float layer1Roughness;
  float layer0Metallic;
  float layer1Metallic;
  float slopeStart;
  float slopeEnd;
  int textureMask;
  float layer0UvScale;
  float layer1UvScale;
  float _pad0;
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec3
#endif
