#define TONEMAP_ACES 0
#define TONEMAP_AGX  1

// Debug view indices - must match the debugViews[] list in RenderSettingsPanel.cpp.
// Only the indirect lighting diagnostics need names on both sides; the rest stay positional.
#define DEBUG_VIEW_AMBIENT_ONLY     10
#define DEBUG_VIEW_AMBIENT_DIFFUSE  11
#define DEBUG_VIEW_AMBIENT_SPECULAR 12
#define DEBUG_VIEW_PROBE_INDEX      13
#define DEBUG_VIEW_PROBE_FALLBACK   14
#define DEBUG_VIEW_VOLUME_COVERAGE  15

// Shared by the shaders and by Render, which has to switch off everything that
// would modify these values on their way to the screen.
#define IS_INDIRECT_DEBUG_VIEW(view) ( \
     (view) == DEBUG_VIEW_AMBIENT_ONLY \
  || (view) == DEBUG_VIEW_AMBIENT_DIFFUSE \
  || (view) == DEBUG_VIEW_AMBIENT_SPECULAR \
  || (view) == DEBUG_VIEW_PROBE_INDEX \
  || (view) == DEBUG_VIEW_PROBE_FALLBACK \
  || (view) == DEBUG_VIEW_VOLUME_COVERAGE)

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
  int tonemapMode;
  float bloomIntensity;
  float ssaoRadius;
  float ssaoIntensity;
  float ssaoBias;
  int fogEnabled;
  float fogDensity;
  float fogHeightFalloff;
  vec3 fogColor;
  float fogStartDistance;
  float fogMaxOpacity;
  float taaClampSigma;
  float ssrIntensity;
  // Meters the diffuse sample point is pushed along the normal before it is
  // looked up in an irradiance volume. Appended at the end so no existing
  // member offset moves.
  float irradianceNormalBias;
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec2
#undef vec3
#undef vec4
#undef mat4
#endif
