#ifdef __cplusplus
#pragma once
#define vec4 glm::vec4
#define mat4 glm::mat4
namespace YAEngine {
#endif

#define CSM_CASCADE_COUNT 4
#define SHADOW_ATLAS_SIZE 8192
#define SHADOW_CASCADE_SIZE 2048
#define MAX_SHADOW_SPOTS 8
#define MAX_SHADOW_POINTS 4
#define SHADOW_SPOT_SIZE 1024
#define SHADOW_POINT_FACE_SIZE 512
#define SHADOW_SPOT_MATRIX_OFFSET CSM_CASCADE_COUNT
#define SHADOW_POINT_MATRIX_OFFSET (CSM_CASCADE_COUNT + MAX_SHADOW_SPOTS)

struct ShadowCascade
{
  mat4 viewProj;
  vec4 splitDepthAndBias;  // x = splitDepth, y = bias, z = normalBias, w = unused
  vec4 atlasViewport;      // xy = offset (normalized), zw = size (normalized)
};

struct SpotShadow
{
  mat4 viewProj;
  vec4 atlasViewport;      // xy = offset (normalized), zw = size (normalized)
  vec4 biasData;           // x = bias, y = normalBias, zw = unused
};

struct PointShadow
{
  mat4 faceViewProj[6];
  vec4 faceAtlasViewport[6];
  vec4 positionFarPlane;   // xyz = light position, w = farPlane (radius)
  vec4 biasData;           // x = bias, y = normalBias, zw = unused
};

struct ShadowBuffer
{
  ShadowCascade cascades[CSM_CASCADE_COUNT];
  vec4 atlasSize;  // x = width, y = height, z = 1/width, w = 1/height
  int shadowsEnabled;
  int cascadeCount;
  int spotShadowCount;
  int pointShadowCount;
  SpotShadow spotShadows[MAX_SHADOW_SPOTS];
  PointShadow pointShadows[MAX_SHADOW_POINTS];
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec4
#undef mat4
#endif
