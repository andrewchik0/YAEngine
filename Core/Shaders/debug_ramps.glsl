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

// A CATEGORY on the hue circle, for a view whose value is an identifier rather than an
// amount. The grey ramp below is deliberately monotonic because brightness there is the
// measurement; here nothing is being measured, so what the display owes is only that a hit is
// findable and that two neighbouring codes do not look alike. Full saturation and value, t in
// 0..1 walking the circle once.
vec3 debugCategoryHue(float t)
{
  vec3 k = fract(t + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0));
  return clamp(abs(k * 6.0 - 3.0) - 1.0, 0.0, 1.0);
}

// Magnitude on a base-10 logarithmic grey scale, shared by every view that has to answer
// "how much" rather than "what colour". Greyscale deliberately: a hue ramp reads as
// decoration and its brightness is not monotonic, so two very different magnitudes can end up
// looking equally dark. Here brightness IS the measurement.
//
// Mid grey is exactly one, which is what a correct value looks like in this engine's linear
// light. Every step of the legend is one order of magnitude either side of it, and anything at
// a hundred or more is painted red - that is the band a firefly clamp would have been hiding.
//
// The legend runs the full scale end to end along the bottom of the frame, so a shade anywhere
// in the image can be matched against it instead of being remembered: blue ticks every decade,
// a green one at exactly one.
vec3 debugLogMagnitude(float magnitude, vec2 uv)
{
  float logMagnitude = log(max(magnitude, PT_DEBUG_LOG_FLOOR)) / log(10.0);

  bool legend = uv.y > 0.94;
  if (legend)
    logMagnitude = mix(PT_DEBUG_LOG_MIN, PT_DEBUG_LOG_MAX, uv.x);

  float t = clamp((logMagnitude - PT_DEBUG_LOG_MIN) / (PT_DEBUG_LOG_MAX - PT_DEBUG_LOG_MIN),
    0.0, 1.0);
  vec3 color = vec3(t);
  if (logMagnitude >= 2.0)
    color = mix(color, vec3(1.0, 0.15, 0.1), 0.75);

  if (legend)
  {
    if (fract(logMagnitude - PT_DEBUG_LOG_MIN) < 0.03)
      color = vec3(0.0, 0.6, 1.0);
    if (abs(logMagnitude) < 0.03)
      color = vec3(0.2, 1.0, 0.2);
  }

  return color;
}

#endif
