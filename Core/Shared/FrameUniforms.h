#ifdef __cplusplus
#pragma once
#define vec2 glm::vec2
#define vec3 glm::vec3
#define vec4 glm::vec4
#define mat4 glm::mat4
namespace YAEngine {
#endif

struct FrameUniforms
{
  mat4 view;
  mat4 proj;
  mat4 invProj;
  mat4 prevView;
  mat4 prevProj;
  vec3 cameraPosition;
  float time;
  vec3 cameraDirection;
  float gamma;
  float exposure;
  int currentTexture;
  float nearPlane;
  float farPlane;
  float fov;
  int screenWidth;
  int screenHeight;
  int ssaoEnabled;
  int ssrEnabled;
  int taaEnabled;
  float jitterX;
  float jitterY;
  int hizMipCount;
  int frameIndex;
  int tileCountX;
  int tileCountY;
  mat4 invView;
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec2
#undef vec3
#undef vec4
#undef mat4
#endif
