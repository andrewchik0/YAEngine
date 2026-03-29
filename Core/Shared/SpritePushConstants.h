#ifdef __cplusplus
#pragma once
#define vec4 glm::vec4
namespace YAEngine {
#endif

struct SpritePushConstants
{
  vec4 positionAndScale; // xyz = world position, w = size
  vec4 color;            // rgba tint
  vec4 shadow;           // x = blur radius (UV), y = aspect ratio, z = shadow opacity, w = isShadow (0/1)
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec4
#endif
