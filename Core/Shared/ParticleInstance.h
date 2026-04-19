#ifdef __cplusplus
#pragma once
#define vec4 glm::vec4
namespace YAEngine {
#endif

// Velocity-aligned streaked sprite for particles.
// Layout packed as three vec4 for std430-friendly alignment (48 bytes).
//   headAndWidth.xyz = world position of the streak head (current particle position)
//   headAndWidth.w   = streak width in world units
//   tail.xyz         = world position of the streak tail (typically head - velocityDir * length)
//   tail.w           = unused (reserved)
//   color            = RGBA, premultiplied alpha expected by the fragment shader
struct ParticleInstance
{
  vec4 headAndWidth;
  vec4 tail;
  vec4 color;
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec4
#endif
