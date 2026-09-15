#ifdef __cplusplus
#pragma once
#define vec3 glm::vec3
#define vec4 glm::vec4
namespace YAEngine {
#endif

#define MAX_POINT_LIGHTS 256
#define MAX_SPOT_LIGHTS 128

// LightBuffer::directionalFlags. The path tracer and the probe baker treat the light as absent.
#define LIGHT_FLAG_RASTER_ONLY 0x1

// Source radius, angular radius and raster only are read by the path tracer and the probe baker
// alone; raster lighting ignores them.
struct PointLight
{
  vec4 positionRadius;    // xyz = position, w = radius
  vec4 colorIntensity;    // xyz = color, w = intensity
  vec4 shadowPad;         // x = shadowIndex (as float, -1 = no shadow), y = source radius, z = raster only (0 or 1), w = padding
};

struct SpotLight
{
  vec4 positionRadius;    // xyz = position, w = radius
  vec4 directionInnerCone; // xyz = direction, w = cos(innerCone)
  vec4 colorOuterCone;    // xyz = color, w = cos(outerCone)
  vec4 intensityShadow;   // x = intensity, y = shadowIndex (as float, -1 = no shadow), z = source radius, w = raster only (0 or 1)
};

struct DirectionalLight
{
  vec4 directionIntensity; // xyz = direction, w = intensity
  vec4 colorPad;           // xyz = color, w = angular radius of the sun disk in radians
};

struct LightBuffer
{
  DirectionalLight directional;
  int pointLightCount;
  int spotLightCount;
  int directionalFlags;    // LIGHT_FLAG_* of the directional light
  int _pad1;
  PointLight pointLights[MAX_POINT_LIGHTS];
  SpotLight spotLights[MAX_SPOT_LIGHTS];
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec3
#undef vec4
#endif
