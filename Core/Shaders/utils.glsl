// Requires common.glsl to be included before this file

// Reversed-Z with an infinite far plane: d = near / viewDistance. Sky texels hold exactly
// 0, so the clamp keeps them a huge finite distance instead of inf (inf - inf is NaN in
// every depth-difference test downstream).
float linearizeDepth(float d)
{
  return u_Frame.nearPlane / max(d, 1e-8);
}

vec3 reconstructViewPos(vec2 screenUV, float depth)
{
  vec2 ndc = screenUV * 2.0 - 1.0;
  vec4 clipPos = vec4(ndc, depth, 1.0);
  vec4 viewPos = u_Frame.invProj * clipPos;
  return viewPos.xyz / viewPos.w;
}

// Screen-space motion vector in UV units.
// curClipPos comes from the jittered projection while prevProj is stored unjittered, so the
// jitter is added back here: the vector must describe geometric motion only. TAA transports a
// per-pixel history anchor along it, so a static camera has to yield exactly zero - otherwise
// the history is resampled every frame by the sub-pixel offset and the image crawls.
// proj[2][0..1] += jitter shifts NDC by exactly -jitter at any depth, hence the plus.
vec2 computeVelocity(vec4 curClipPos, vec4 prevClipPos)
{
  vec2 curNDC = curClipPos.xy / curClipPos.w + vec2(u_Frame.jitterX, u_Frame.jitterY);
  vec2 prevNDC = prevClipPos.xy / prevClipPos.w;
  return (curNDC - prevNDC) * 0.5;
}

// --- Emissive G-buffer packing ---
// Emissive reuses GBuffer0 whole: rgb keeps the hue at unit peak, alpha carries the peak
// itself. That costs the metallic channel, which is why the pixel also has to switch its
// shading model - an emissive texel is shaded as pure emission, never as PBR.
const float EMISSIVE_MAX = 64.0;
// A texel emitting more than a white lambertian surface under unit light is bright enough
// that dropping its PBR response is invisible. Below that the surface still reads as lit,
// so it stays on the PBR path and the emissive contribution is dropped instead.
const float EMISSIVE_SHADING_CUTOFF = 1.0;

float luminance(vec3 c)
{
  return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Log encoding: 8 bits of alpha hold ~1% relative error across the whole range, where a
// linear split would quantise everything below 1.0 into four steps.
vec4 encodeEmissive(vec3 emissive)
{
  float peak = min(max(max(emissive.r, emissive.g), emissive.b), EMISSIVE_MAX);
  vec3 hue = peak > 0.0 ? emissive / peak : vec3(0.0);
  return vec4(hue, log2(1.0 + peak) / log2(1.0 + EMISSIVE_MAX));
}

vec3 decodeEmissive(vec4 gbuffer0)
{
  return gbuffer0.rgb * (exp2(gbuffer0.a * log2(1.0 + EMISSIVE_MAX)) - 1.0);
}

// --- Shading model ---
// Packed into the 2-bit alpha of GBuffer1, so only four values exist. The encode side
// writes the float, the lighting pass reads the int back.
const float SHADING_MODEL_PBR = 0.0;
const float SHADING_MODEL_UNLIT = 1.0 / 3.0;
const float SHADING_MODEL_EMISSIVE = 2.0 / 3.0;

const int SHADING_PBR = 0;
const int SHADING_UNLIT = 1;
const int SHADING_EMISSIVE = 2;

int decodeShadingModel(float packed)
{
  return int(packed * 3.0 + 0.5);
}
