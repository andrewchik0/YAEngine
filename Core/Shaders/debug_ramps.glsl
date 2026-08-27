#ifndef DEBUG_RAMPS_GLSL
#define DEBUG_RAMPS_GLSL

// Heat ramp for debug views showing "how much fell through to the fallback":
// black = 0.0 (fully covered), red = 0.33, yellow = 0.66,
// white = 1.0 (nothing but the fallback contributed).
// Shared by the probe fallback view in deferred lighting and the SSGI fallback
// weight view in the tonemap pass.
vec3 debugFallbackHeat(float amount)
{
  float t = clamp(amount, 0.0, 1.0) * 3.0;
  vec3 c = mix(vec3(0.0), vec3(1.0, 0.0, 0.0), clamp(t, 0.0, 1.0));
  c = mix(c, vec3(1.0, 1.0, 0.0), clamp(t - 1.0, 0.0, 1.0));
  return mix(c, vec3(1.0), clamp(t - 2.0, 0.0, 1.0));
}

#endif
