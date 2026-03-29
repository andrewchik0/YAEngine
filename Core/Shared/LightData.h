#ifdef __cplusplus
#pragma once
#define vec3 glm::vec3
#define vec4 glm::vec4
namespace YAEngine {
#endif

#define MAX_POINT_LIGHTS 256
#define MAX_SPOT_LIGHTS 128

struct PointLight
{
  vec4 positionRadius;    // xyz = position, w = radius
  vec4 colorIntensity;    // xyz = color, w = intensity
};

struct SpotLight
{
  vec4 positionRadius;    // xyz = position, w = radius
  vec4 directionInnerCone; // xyz = direction, w = cos(innerCone)
  vec4 colorOuterCone;    // xyz = color, w = cos(outerCone)
  vec4 intensityPad;      // x = intensity, yzw = padding
};

struct DirectionalLight
{
  vec4 directionIntensity; // xyz = direction, w = intensity
  vec4 colorPad;           // xyz = color, w = padding
};

struct LightBuffer
{
  DirectionalLight directional;
  int pointLightCount;
  int spotLightCount;
  int _pad0;
  int _pad1;
  PointLight pointLights[MAX_POINT_LIGHTS];
  SpotLight spotLights[MAX_SPOT_LIGHTS];
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec3
#undef vec4
#endif
