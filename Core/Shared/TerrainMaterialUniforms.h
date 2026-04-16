#ifdef __cplusplus
#pragma once
#define vec3 glm::vec3
#define vec4 glm::vec4
namespace YAEngine {
#endif

#define MAX_ROAD_SEGMENTS 64

struct TerrainMaterialUniforms
{
  vec3 layer0Albedo;
  float layer0Roughness;
  vec3 layer1Albedo;
  float layer1Roughness;
  vec3 layer2Albedo;
  float layer2Roughness;
  float layer0Metallic;
  float layer1Metallic;
  float layer2Metallic;
  float slopeStart;
  float slopeEnd;
  int textureMask;
  float layer0UvScale;
  float layer1UvScale;
  float layer2UvScale;
  int roadSegmentCount;
  float shoulderInnerRadius;
  float shoulderOuterRadius;
  float shoulderWarpAmplitude;
  float shoulderWarpScale;
  float _pad0;
  float _pad1;
  vec4 roadSegments[MAX_ROAD_SEGMENTS];
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec3
#undef vec4
#endif
