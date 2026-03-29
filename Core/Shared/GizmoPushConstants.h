#ifdef __cplusplus
#pragma once
#define vec4 glm::vec4
#define mat4 glm::mat4
namespace YAEngine {
#endif

struct GizmoPushConstants
{
  mat4 world;
  vec4 color;
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec4
#undef mat4
#endif
