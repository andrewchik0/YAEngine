#ifdef __cplusplus
#pragma once
#define vec4 glm::vec4
namespace YAEngine {
#endif

#define MAX_LIGHT_PROBES 16

struct LightProbeInfo
{
  vec4 positionShape;   // xyz = world position, w = shape (0.0 = sphere, 1.0 = box)
  vec4 extentsFade;     // xyz = extents (radius for sphere, half-extents for box), w = fadeDistance
  int arrayIndex;       // index into cubemap array (0 = skybox, 1..N = probes)
  int priority;
  int _pad0;
  int _pad1;
};

struct LightProbeBuffer
{
  int probeCount;
  int _pad0;
  int _pad1;
  int _pad2;
  LightProbeInfo probes[MAX_LIGHT_PROBES];
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec4
#endif
