#ifdef __cplusplus
#pragma once
#define vec4 glm::vec4
namespace YAEngine {
#endif

#define MAX_REFLECTION_PROBES 16

struct ReflectionProbeInfo
{
  vec4 positionShape;   // xyz = world position, w = shape (0.0 = sphere, 1.0 = box)
  vec4 extentsFade;     // xyz = extents (radius for sphere, half-extents for box), w = fadeDistance
  vec4 orientation;     // rotation quaternion (xyz, w) taken from the entity transform
  int arrayIndex;       // index into cubemap array (0 = skybox, 1..N = probes)
  int priority;
  int parallaxCorrection; // 1 = reproject reflections onto the proxy volume
  int _pad0;              // keeps the struct at 64 bytes (std430 needs a multiple of 16)
};

struct ReflectionProbeBuffer
{
  int probeCount;
  int _pad0;
  int _pad1;
  int _pad2;
  ReflectionProbeInfo probes[MAX_REFLECTION_PROBES];
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec4
#endif
