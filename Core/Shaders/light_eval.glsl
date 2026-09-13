#ifndef LIGHT_EVAL_GLSL
#define LIGHT_EVAL_GLSL

// What one analytical light is worth at a point, shadowing excluded: the direction to it,
// the distance to it and its attenuated radiance. Pure math over the vectors LightData.h
// packs a light into - no descriptor is declared here, which is what lets the ray
// generation shader of the path tracer include it while the light SSBO sits at a completely
// different set and binding than it does for deferred lighting.
//
// It exists so the two paths cannot drift on what a light reaches. A traced image is only
// comparable with the rasterized one if both agree on the falloff curve and the cone, and
// the falloff is a windowed inverse-square-free one the engine picked - it is not
// something a second implementation would arrive at on its own.

#include "../Shared/LightData.h"

// The packed vec4s go in, not the struct they came out of. A struct parameter would make
// the caller load the whole record even where it reads three of its four vectors, and the
// deferred loop is the hottest light loop in the engine - this way its memory access
// pattern is exactly what it was before the math moved here.

// Returns false when the point is outside the light's radius, in which case nothing else
// is written. The falloff is (1 - d^2/r^2)^2, which reaches exactly zero at the radius, so
// a light can be culled by range without a visible edge.
bool evaluatePointLight(vec4 positionRadius, vec4 colorIntensity, vec3 worldPos,
  out vec3 L, out float lightDistance, out vec3 radiance)
{
  vec3 toLight = positionRadius.xyz - worldPos;
  float lightRadius = positionRadius.w;

  lightDistance = length(toLight);
  if (lightDistance > lightRadius) return false;
  L = toLight / lightDistance;

  float att = 1.0 - (lightDistance * lightDistance) / (lightRadius * lightRadius);
  att = att * att;
  radiance = colorIntensity.rgb * colorIntensity.w * att;
  return true;
}

// Same falloff plus the cone, which is a linear ramp in cosine between the inner and outer
// angles. Outside the outer cone the ramp clamps to zero and the light still "reaches" the
// point - the caller sees a zero radiance rather than a false return, exactly as the
// deferred loop does, so the two cannot disagree on the boundary case.
bool evaluateSpotLight(vec4 positionRadius, vec4 directionInnerCone, vec4 colorOuterCone,
  float intensity, vec3 worldPos, out vec3 L, out float lightDistance, out vec3 radiance)
{
  vec3 toLight = positionRadius.xyz - worldPos;
  float lightRadius = positionRadius.w;

  lightDistance = length(toLight);
  if (lightDistance > lightRadius) return false;
  L = toLight / lightDistance;

  vec3 lightDir = directionInnerCone.xyz;
  float innerCos = directionInnerCone.w;
  float outerCos = colorOuterCone.w;
  float theta = dot(L, -lightDir);
  float spotFactor = clamp((theta - outerCos) / (innerCos - outerCos), 0.0, 1.0);

  float att = 1.0 - (lightDistance * lightDistance) / (lightRadius * lightRadius);
  att = att * att;
  radiance = colorOuterCone.rgb * intensity * att * spotFactor;
  return true;
}

// No distance and no attenuation: the sun is infinitely far away, so its radiance is the
// same everywhere and only its direction has to be handed back.
vec3 evaluateDirectionalLight(vec4 directionIntensity, vec3 color, out vec3 L)
{
  L = normalize(-directionIntensity.xyz);
  return color * directionIntensity.w;
}

#endif
