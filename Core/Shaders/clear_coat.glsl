#ifndef CLEAR_COAT_GLSL
#define CLEAR_COAT_GLSL

// The clear coat as every pass reads it: its Fresnel and its G-buffer codec, shared by the G-buffer
// pass, deferred lighting, SSR and the path tracer so a coat texel means the same to all of them.
//
// A coat texel is a PBR texel of a material with a coat. GBuffer1 describes the coat - its normal, its
// roughness and SHADING_MODEL_CLEAR_COAT - and GBuffer2 the surface under it: R the coat weight, G the
// surface roughness, BA the surface normal as xy in a basis around the coat normal, z reconstructed.
// Every other texel writes zeros to GBuffer2.

#include "octahedron.glsl"
#include "../Shared/MaterialUniforms.h"

// A dielectric of IOR 1.5.
const float CLEAR_COAT_F0 = 0.04;

// Perceptual roughness analytic lights see a coat with at least. GGX at roughness 0 has no density
// outside the exact mirror direction, so a point light would leave no highlight at all.
const float CLEAR_COAT_MIN_LIGHT_ROUGHNESS = 0.045;

// Schlick with F0 0.04, scaled by the coat weight. cosTheta is taken against the coat normal.
float clearCoatFresnel(float weight, float cosTheta)
{
  float t = clamp(1.0 - cosTheta, 0.0, 1.0);
  float t2 = t * t;
  return weight * (CLEAR_COAT_F0 + (1.0 - CLEAR_COAT_F0) * (t2 * t2 * t));
}

// The weight as GBuffer2.R stores it. A coat whose weight rounds to zero there is no coat, and the
// G-buffer pass writes such a texel as plain PBR rather than as a coat no decoder can see.
float quantizeClearCoatWeight(float weight)
{
  return round(clamp(weight, 0.0, 1.0) * CLEAR_COAT_WEIGHT_STEPS) / CLEAR_COAT_WEIGHT_STEPS;
}

// The cosine-weighted hemispherical mean of clearCoatFresnel: the Schlick term integrates to 1 / 21.
float clearCoatAverageFresnel(float weight)
{
  return weight * (CLEAR_COAT_F0 + (1.0 - CLEAR_COAT_F0) / 21.0);
}

// An orthonormal basis around the unit vector n (Duff et al., "Building an Orthonormal Basis,
// Revisited", JCGT 2017). Deterministic in n, so the encoder and the decoders build the same basis
// from the same normal.
void clearCoatBasis(vec3 n, out vec3 t, out vec3 b)
{
  float s = n.z >= 0.0 ? 1.0 : -1.0;
  float a = -1.0 / (s + n.z);
  float c = n.x * n.y * a;
  t = vec3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
  b = vec3(c, s + n.y * n.y * a, -n.y);
}

// n as the 10-bit GBuffer1 channels store it and octDecode reads it back.
vec3 quantizeGBufferNormal(vec3 n)
{
  vec2 stored = round((octEncode(n) * 0.5 + 0.5) * 1023.0) / 1023.0;
  return octDecode(stored * 2.0 - 1.0);
}

// A signed value in an 8-bit UNORM channel, centred so code 127 is exactly zero.
float encodeCentredUnorm8(float x)
{
  return (round(clamp(x, -1.0, 1.0) * 127.0) + 127.0) / 255.0;
}

// The code of an exact zero.
const float CENTRED_UNORM8_ZERO = 127.0 / 255.0;

float decodeCentredUnorm8(float v)
{
  return clamp((round(v * 255.0) - 127.0) / 127.0, -1.0, 1.0);
}

// GBuffer2 of a coat texel. weight is quantizeClearCoatWeight's, coatNormal the unit normal written to
// GBuffer1, baseNormal the unit shading normal of the surface; normalMapped false says the two are the
// same normal, which is then written as the exact zero code rather than trusted to come out of the
// encode as one - GBuffer1's own rounding leaves the reconstructed basis a fraction of a degree off,
// about as far as one 8-bit step. Otherwise the basis is built around the coat normal as the decoders
// will read it.
vec4 encodeClearCoatGBuffer(float weight, float baseRoughness, vec3 coatNormal, vec3 baseNormal,
  bool normalMapped)
{
  if (!normalMapped)
    return vec4(weight, baseRoughness, CENTRED_UNORM8_ZERO, CENTRED_UNORM8_ZERO);

  vec3 n = quantizeGBufferNormal(coatNormal);
  vec3 t;
  vec3 b;
  clearCoatBasis(n, t, b);

  vec2 xy = vec2(dot(baseNormal, t), dot(baseNormal, b));
  // Below the coat's horizon there is no z to reconstruct, so such a normal is laid onto the horizon.
  if (dot(baseNormal, n) < 0.0)
    xy /= sqrt(max(dot(xy, xy), 1e-8));

  return vec4(weight, baseRoughness, encodeCentredUnorm8(xy.x), encodeCentredUnorm8(xy.y));
}

// The surface normal under a coat texel, from its GBuffer2 texel (read unfiltered) and the coat normal
// decoded out of GBuffer1.
vec3 decodeClearCoatBaseNormal(vec4 gbuffer2, vec3 coatNormal)
{
  vec3 t;
  vec3 b;
  clearCoatBasis(coatNormal, t, b);

  vec2 xy = vec2(decodeCentredUnorm8(gbuffer2.b), decodeCentredUnorm8(gbuffer2.a));
  // sqrt is not trusted on an exact zero, see importanceSampleGGX in pbr.glsl.
  float z2 = 1.0 - dot(xy, xy);
  float z = z2 > 0.0 ? sqrt(z2) : 0.0;
  return normalize(t * xy.x + b * xy.y + coatNormal * z);
}

#endif
