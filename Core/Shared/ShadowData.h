#ifdef __cplusplus
#pragma once
#define vec4 glm::vec4
#define mat4 glm::mat4
namespace YAEngine {
#endif

#define CSM_CASCADE_COUNT 4
#define SHADOW_ATLAS_SIZE 8192
#define SHADOW_CASCADE_SIZE 2048

struct ShadowCascade
{
  mat4 viewProj;
  vec4 splitDepthAndBias;  // x = splitDepth, y = bias, z = normalBias, w = unused
  vec4 atlasViewport;      // xy = offset (normalized), zw = size (normalized)
};

struct ShadowBuffer
{
  ShadowCascade cascades[CSM_CASCADE_COUNT];
  vec4 atlasSize;  // x = width, y = height, z = 1/width, w = 1/height
  int shadowsEnabled;
  int cascadeCount;
  int _pad0;
  int _pad1;
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec4
#undef mat4
#endif
