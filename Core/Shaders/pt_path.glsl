#ifndef PT_PATH_GLSL
#define PT_PATH_GLSL

// The path estimator from an already known surface vertex on, shared so that everything that
// has to agree with the path traced reference runs the same math: pt_main.rgen starts it at the
// vertex its G-buffer describes, any other consumer wherever its own first ray landed.
//
// No version or extension directives here, for the reason raytracing_common.glsl gives. What a
// consumer owes this file:
//  - raytracing_common.glsl included under RT_BINDLESS, and light_eval.glsl included before
//    u_Lights is declared, since LightBuffer comes from there;
//  - declared ahead of this file: u_Lights (LightBuffer), u_Skybox (samplerCube), payload
//    (RayTracingPayload at location 0) and shadowPayload (ShadowRayPayload at location 1);
//  - a shader binding table whose hit group record 0 is pathtrace.rchit + pathtrace.rahit, with
//    pathtrace.rmiss at miss index PT_PRIMARY_MISS_INDEX and pt_shadow.rmiss at
//    PT_SHADOW_MISS_INDEX. A wrong order still builds and runs, and silently zeroes next event
//    estimation;
//  - its own NaN/Inf guard on the radiance it keeps: clampContribution drops a bad addend, but
//    nothing here promises the sum is finite.
// Non-finite tracking is armed only inside tracePath, from PathSettings, and g_NonFiniteCode is
// never reset between tracePath calls: with several paths in one invocation it names the first
// non-finite value of any of them.

#include "utils.glsl"
#include "pbr.glsl"
#include "random.glsl"
#include "light_eval.glsl"
#include "../Shared/PathTraceData.h"

const float PT_RAY_TMIN = 1e-3;
const float PT_RAY_TMAX = 100000.0;
// Multiplied by the magnitude of the coordinate, see offsetRayOrigin.
const float PT_ORIGIN_OFFSET = 1e-3;
// Floor on the Russian roulette survival probability. Without it a nearly black throughput
// would divide by nearly zero on the rare frame the path survives, which is a firefly the
// roulette itself created.
const float PT_RR_MIN_SURVIVAL = 0.05;
// Keeps the lobe choice away from a probability of zero on a surface that still has energy
// in that lobe - dividing by it is what would blow up otherwise.
const float PT_MIN_LOBE_PROBABILITY = 0.1;

// --- Sampling ---
//
// Every dimension of every path vertex comes from the plain PCG stream in random.glsl, and
// deliberately so: section 3.5 of NVIDIA's DLSS-RR Integration Guide requires it. Ray
// reconstruction "assumes independent samples and requires that sampling used to generate
// Inputs must have minimal correlation both spatially and temporally", asks for "high quality
// hash functions and white noise", and lists "sharing of sampling patterns across the screen"
// among the practices to avoid. Blue noise is allowed only where the period is large enough.
//
// A low-discrepancy stream was tried here - the R sequence walked along a Hilbert curve, the
// machinery gtao.frag uses - on the theory that a temporal denoiser at one sample per pixel
// needs its input stratified. That is the opposite of what the guide says, and the
// construction violated it twice over: the curve tiles the screen every 64 pixels, so
// neighbouring pixels drew deterministically related samples, and the Cranley-Patterson
// rotation was constant across the screen by design.
//
// pcgHash is PCG-RXS-M-XS from Jarzynski & Olano, JCGT 2020 - the very issue the guide cites
// when it asks for a hash with few 'bigcrush' failures.
//
// The lever the guide does endorse for noise at this sample count is variance reduction in
// the estimator itself, ReSTIR DI and GI by name, not a different point set.

// Pushes a ray origin off the surface along the geometric normal, scaled with the magnitude
// of the coordinate so a point far from the world origin still gets an offset above the
// float spacing there. The first vertex needs it most: its position comes back through the
// depth buffer and carries the reconstruction error with it, not just the triangle's.
vec3 offsetRayOrigin(vec3 position, vec3 normal)
{
  float scale = max(1.0, max(abs(position.x), max(abs(position.y), abs(position.z))));
  return position + normal * (PT_ORIGIN_OFFSET * scale);
}

// The one deliberate bias in the estimator. A single sample that lands on a small bright
// source through a tight specular lobe can carry hundreds of times the mean and would take
// thousands of frames to average back out, so what one bounce may add is capped. Zero
// switches it off and the tracer is unbiased again - which is the setting to compare
// against when a converged image looks too dark.
vec3 clampContribution(vec3 contribution, float fireflyClamp)
{
  // Ahead of the ceiling test, so it still runs with the clamp switched off, and ahead of any
  // arithmetic, because min(NaN, k) returns k: a NaN used to leave here as a neutral k in all
  // three channels - a white pixel the end-of-main guard can no longer tell from a legitimate
  // one. Rejecting the term here also costs less than that guard does. It throws away the
  // whole path's radiance across every bounce; this drops only the addend that went bad.
  if (any(isnan(contribution)) || any(isinf(contribution)))
    return vec3(0.0);

  if (fireflyClamp <= 0.0)
    return contribution;

  // Scaled as a whole, not clamped per channel. min() on each channel separately turns
  // anything over the ceiling in all three into neutral white and throws the material's hue
  // away with it. The excess is measured on the largest channel rather than on luminance
  // because luminance weights blue at 0.072 - a blue firefly of 1000 has a luminance of 72 and
  // would sail through a ceiling of 100 untouched.
  float peak = max(contribution.r, max(contribution.g, contribution.b));
  if (peak <= fireflyClamp)
    return contribution;

  return contribution * (fireflyClamp / peak);
}

// Records one unclamped contribution against the running maximum, so the debug views can name
// the bounce that produced the largest single addition rather than only its size.
void recordDebugContribution(vec3 contribution, int bounce,
  inout float maxContribution, inout float maxBounce)
{
  float value = luminance(contribution);
  // A non-finite contribution loses EVERY comparison it takes part in - NaN loses them all -
  // so the running maximum would keep reading like a healthy frame while the pixel it came
  // from is already poisoned. Swapping it for a magnitude far above the ramp is what makes
  // the view that exists to find the largest contribution able to see the worst one.
  if (isnan(value) || isinf(value))
    value = PT_DEBUG_NONFINITE_MAGNITUDE;

  if (value > maxContribution)
  {
    maxContribution = value;
    maxBounce = float(bounce);
  }
}

// --- PT_DEBUG_NONFINITE state ---
//
// Globals rather than an inout parameter threaded through the sampling functions: the codes
// that matter most fire from inside sampleBrdfDirection's GGX branch, on both sides of an
// early return, and growing every signature there by two diagnostic arguments would put the
// instrument into the shading code instead of alongside it.
bool g_NonFiniteTracking = false;
int g_NonFiniteCode = PT_NF_NONE;
// The bounce the first non-finite fired on, and the bounce the loop is currently on. -1 in
// the first means nothing fired, which is what the view stores for a healthy pixel.
int g_NonFiniteBounce = -1;
int g_CurrentBounce = 0;

// Records the first non-finite quantity of a path and nothing after it, so the code names
// where the poison entered rather than everywhere it spread to.
//
// isnan/isinf explicitly, never a relational test: every comparison against NaN is false, and
// that is precisely how recordDebugContribution above went blind to the failure it was
// written to catch.
void recordNonFinite(vec3 value, int code)
{
  if (!g_NonFiniteTracking || g_NonFiniteCode != PT_NF_NONE)
    return;

  if (!any(isnan(value)) && !any(isinf(value)))
    return;

  g_NonFiniteCode = code;
  g_NonFiniteBounce = g_CurrentBounce;
}

void recordNonFinite(float value, int code)
{
  recordNonFinite(vec3(value), code);
}

// Everything one hit surface is worth, decoded the way gbuffer.frag decodes the same
// material out of its own descriptor set. Keeping the two in step is what makes a traced
// image comparable with the rasterized one at all.
struct PathSurface
{
  vec3 albedo;
  float metallic;
  float roughness;
  vec3 emissive;
  // True for a texel the G-buffer pass would have written as pure emission, which has given
  // up its PBR response - so the path ends there, exactly as the raster shading does.
  bool emissiveTexel;
};

PathSurface resolveHitMaterial(RayHitGeometry hit, vec2 barycentrics)
{
  PathSurface surface;
  surface.albedo = vec3(0.72);
  surface.metallic = 0.0;
  surface.roughness = 1.0;
  surface.emissive = vec3(0.0);
  surface.emissiveTexel = false;

  if (hit.materialIndex >= uint(u_Materials.length()))
    return surface;

  RayTracingMaterialRecord material = u_Materials[hit.materialIndex];
  vec2 texCoord = hitTexCoord(hit, barycentrics) * material.uvScale;

  // Mip 0 on every fetch: a ray tracing invocation has no derivatives, so an implicit LOD
  // is undefined here. It is also why a traced surface aliases where the raster one does
  // not - a ray differential footprint is what would fix that.
  vec3 baseColor = material.albedo;
  if ((material.textureMask & RT_MATERIAL_BASE_COLOR) != 0u)
    baseColor *= textureLod(u_BindlessTextures[nonuniformEXT(material.baseColorIndex)],
      texCoord, 0.0).rgb;

  // gbuffer.frag decodes base color with the display gamma rather than through an sRGB
  // image view, so this has to as well or every traced surface reads far too bright.
  surface.albedo = pow(baseColor, vec3(u_Frame.gamma));

  // Absent maps fall back to white, which is what the raster mix(1.0, sample, hasTexture)
  // collapses to - the fallback is the identity for both the metallic and roughness scales.
  vec4 metallicSample = vec4(1.0);
  if ((material.textureMask & RT_MATERIAL_METALLIC) != 0u)
    metallicSample = textureLod(u_BindlessTextures[nonuniformEXT(material.metallicIndex)],
      texCoord, 0.0);
  surface.metallic = material.metallic * metallicSample.b;

  float roughnessSample = 1.0;
  if ((material.textureMask & RT_MATERIAL_ROUGHNESS) != 0u)
    roughnessSample = textureLod(u_BindlessTextures[nonuniformEXT(material.roughnessIndex)],
      texCoord, 0.0).r;
  // A combined ORM map keeps roughness in green and overrides the separate map entirely.
  surface.roughness = material.roughness
    * (((material.textureMask & RT_MATERIAL_COMBINED) != 0u) ? metallicSample.g : roughnessSample);

  if ((material.textureMask & RT_MATERIAL_EMISSIVE_SHADING) != 0u)
  {
    vec3 emissive = material.emissivity;
    if ((material.textureMask & RT_MATERIAL_EMISSIVE_MAP) != 0u)
      emissive *= textureLod(u_BindlessTextures[nonuniformEXT(material.emissiveIndex)],
        texCoord, 0.0).rgb;

    // The same per-texel decision the G-buffer pass makes: below the cutoff the emission is
    // dropped and the texel stays PBR, above it the texel IS the emitter.
    if (luminance(emissive) > EMISSIVE_SHADING_CUTOFF)
    {
      surface.emissive = emissive;
      surface.emissiveTexel = true;
    }
  }

  return surface;
}

// One shadow ray. TerminateOnFirstHit plus SkipClosestHitShader leaves the shadow miss
// shader as the only one that can run, which is what makes the answer a single bit. The
// any-hit is deliberately still live: it is the alpha cutout, and it is the reason this
// tracer gets foliage shadows right where the raster shadow atlas needs a separate pipeline
// variant for the same thing.
float traceShadowRay(vec3 origin, vec3 direction, float maxDistance)
{
  shadowPayload.visible = 0u;

  traceRayEXT(u_Tlas,
    gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,
    RT_MASK_OPAQUE,
    0u, // sbtRecordOffset: one hit group, and every instance names it
    0u, // sbtRecordStride: no per-geometry records to step over
    uint(PT_SHADOW_MISS_INDEX),
    origin, PT_RAY_TMIN, direction, maxDistance,
    1); // payload location

  return float(shadowPayload.visible);
}

struct PathLightSample
{
  vec3 direction;
  // Attenuated and cone-shaped, shadowing excluded - that is the shadow ray's job.
  vec3 radiance;
  // How far the shadow ray may travel before the light itself is in the way.
  float lightDistance;
  // Probability this light was the one picked, which the estimator divides back out.
  float selectionPdf;
};

// Flattens the light buffer into one candidate list so the two passes below can walk it with
// the same code: candidate 0 is the directional light, then the point lights, then the spots.
// False means the light does not reach the point at all.
bool evaluateLightCandidate(int candidate, vec3 worldPos,
  out vec3 L, out float lightDistance, out vec3 radiance)
{
  int pointCount = min(u_Lights.pointLightCount, MAX_POINT_LIGHTS);

  if (candidate == 0)
  {
    // The sun has no range to cull against, so it always reaches. One that is switched off
    // carries zero intensity and drops out on the weight test instead.
    radiance = evaluateDirectionalLight(u_Lights.directional.directionIntensity,
      u_Lights.directional.colorPad.rgb, L);
    lightDistance = PT_RAY_TMAX;
    return true;
  }

  int index = candidate - 1;
  if (index < pointCount)
    return evaluatePointLight(u_Lights.pointLights[index].positionRadius,
      u_Lights.pointLights[index].colorIntensity, worldPos, L, lightDistance, radiance);

  index -= pointCount;
  // A point past the outer cone comes back true with zero radiance rather than false, and
  // the weight test culls it - one rule for both kinds of miss.
  return evaluateSpotLight(u_Lights.spotLights[index].positionRadius,
    u_Lights.spotLights[index].directionInnerCone, u_Lights.spotLights[index].colorOuterCone,
    u_Lights.spotLights[index].intensityShadow.x, worldPos, L, lightDistance, radiance);
}

// One light per path vertex, picked from the lights that reach the point at all with a
// probability proportional to the luminance of their UNSHADOWED radiance - the only part of
// the contribution available for free, since it was computed for the range test anyway.
// Importance sampling by it therefore costs nothing and beats a uniform pick wherever one
// light dominates the point, which is the normal case in a lit interior. The estimator
// divides by the resulting pdf, so a bad weight costs variance and never bias.
//
// TWO passes and ONE uniform, rather than the one pass and one-uniform-per-candidate that
// weighted reservoir sampling needs. The distribution is identical; what the single uniform
// buys is that it can come from the low-discrepancy stream, and a reservoir's per-candidate
// stream cannot - it consumes an unpredictable number of dimensions. The second pass stops as
// soon as the cumulative weight crosses the target, so it walks half the candidates on
// average, and the first one does no shadow ray and no BRDF work.
//
// Delta lights in v1: a point light's radius is its falloff range and not an emitter size,
// so nothing samples an area here and every shadow is hard. Sphere light sampling would
// replace the direction, the distance and the pdf below and leave the rest of the loop
// untouched.
bool selectLight(vec3 worldPos, float lightSelector, out PathLightSample result)
{
  int candidateCount = 1
    + min(u_Lights.pointLightCount, MAX_POINT_LIGHTS)
    + min(u_Lights.spotLightCount, MAX_SPOT_LIGHTS);

  result.direction = vec3(0.0, 1.0, 0.0);
  result.radiance = vec3(0.0);
  result.lightDistance = PT_RAY_TMAX;
  result.selectionPdf = 1.0;

  float totalWeight = 0.0;
  for (int candidate = 0; candidate < candidateCount; candidate++)
  {
    vec3 L;
    float lightDistance;
    vec3 radiance;
    if (!evaluateLightCandidate(candidate, worldPos, L, lightDistance, radiance))
      continue;

    totalWeight += max(luminance(radiance), 0.0);
  }

  if (totalWeight <= 0.0)
    return false;

  float target = lightSelector * totalWeight;
  float cumulative = 0.0;
  float chosenWeight = 0.0;

  for (int candidate = 0; candidate < candidateCount; candidate++)
  {
    vec3 L;
    float lightDistance;
    vec3 radiance;
    if (!evaluateLightCandidate(candidate, worldPos, L, lightDistance, radiance))
      continue;

    float weight = max(luminance(radiance), 0.0);
    if (weight <= 0.0)
      continue;

    // Written for every positive candidate rather than only for the crossing one, so a
    // selector that rounds up to the total still leaves the last valid candidate here
    // instead of nothing at all.
    result.direction = L;
    result.radiance = radiance;
    result.lightDistance = lightDistance;
    chosenWeight = weight;

    cumulative += weight;
    if (cumulative > target)
      break;
  }

  result.selectionPdf = chosenWeight / totalWeight;
  return true;
}

// Next event estimation at one path vertex: one light, one shadow ray, weighted back up by
// the probability that light was picked.
//
// There is no MIS weight anywhere in this tracer and none is needed. The analytical lights
// are delta lights, which BRDF sampling can never hit, and the environment is only ever
// collected when a bounce ray misses, which next event estimation never samples. The two
// strategies have disjoint supports, so every contribution is counted exactly once. Adding
// environment NEE - which is what a bright sky needs - is what would make MIS weights
// mandatory.
vec3 estimateDirectLight(vec3 worldPos, vec3 N, vec3 V, vec3 albedo, float metallic,
  float roughness, vec3 f0, float NdotV, float lightSelector)
{
  PathLightSample light;
  if (!selectLight(worldPos, lightSelector, light))
    return vec3(0.0);

  // Backfacing surfaces cost nothing but the test - no shadow ray is traced for them.
  if (dot(N, light.direction) <= 0.0)
    return vec3(0.0);

  float visibility = traceShadowRay(offsetRayOrigin(worldPos, N), light.direction,
    light.lightDistance);
  if (visibility <= 0.0)
    return vec3(0.0);

  // The same BRDF evaluation deferred_lighting.frag runs per light, from the same pbr.glsl:
  // the D, G and F terms, the k remap and the kD split are one implementation, so a lit
  // surface cannot shade differently just because it was reached by a ray.
  float alpha = roughness * roughness;

  vec3 diffuse;
  vec3 specular;
  vec3 total = evaluateDirectLightSplit(N, V, light.direction, light.radiance, albedo,
    metallic, roughness, alpha, f0, NdotV, diffuse, specular);

  // A delta lobe cannot see a delta light: the probability that the mirror direction lands
  // exactly on a point or directional source is zero, so the correct specular contribution
  // here is not small, it is identically nothing. The split evaluation exists precisely so
  // the diffuse half - which a dielectric mirror still has - survives that. See
  // PT_DELTA_MAX_ALPHA. The raster path needs no such branch: it never samples the lobe, so
  // its narrow highlight is the intended stand-in for a light that has no area.
  if (alpha <= PT_DELTA_MAX_ALPHA)
    total = diffuse;

  return total / light.selectionPdf;
}

// How much of the surface response is specular, which is what decides how often the
// specular lobe is sampled. Fresnel at normal incidence against the diffuse albedo is the
// cheapest estimate that gets metal (no diffuse lobe at all) and a dielectric (mostly
// diffuse) both right. A surface with a diffuse lobe keeps the probability away from zero
// and one, since dividing by it is what would blow up otherwise; a metal is left at exactly
// one because its diffuse lobe carries no energy to lose.
float specularLobeProbability(vec3 albedo, vec3 f0, float metallic, float roughness)
{
  float diffuseWeight = luminance(albedo) * (1.0 - metallic);
  float specularWeight = luminance(f0);

  if (diffuseWeight <= 0.0)
    return 1.0;

  float probability = specularWeight / max(specularWeight + diffuseWeight, 1e-5);

  // Fresnel at normal incidence is the right measure on a rough surface and badly wrong on a
  // smooth dark dielectric. Bistro's exterior glass is the case that exposed it: F0 is 0.04
  // against a nearly black albedo, so the coin lands on the specular lobe about four percent
  // of the time and pays 1/p when it does - and on a mirror-smooth surface that lobe carries
  // essentially everything the eye sees there. The result is salt-and-pepper spikes at 25x
  // weight, which a temporal denoiser does not integrate away, it amplifies. The probability
  // is therefore floored wherever the surface is smooth enough for specular to dominate, and
  // the floor fades out with roughness because a rough surface neither concentrates its lobe
  // nor produces the spikes.
  //
  // ANY probability in (0, 1) leaves the estimator unbiased: the lobe weight divides by
  // exactly the probability that was used to pick it, so moving it redistributes variance
  // between the two lobes and never touches the mean. PT Reference converges to the same
  // image, only faster on glass.
  float smoothFloor = mix(PT_SMOOTH_LOBE_FLOOR, 0.0,
    smoothstep(PT_LOBE_FLOOR_ROUGHNESS_MIN, PT_LOBE_FLOOR_ROUGHNESS_MAX, roughness));
  probability = max(probability, smoothFloor);

  return clamp(probability, PT_MIN_LOBE_PROBABILITY, 1.0 - PT_MIN_LOBE_PROBABILITY);
}

// Picks the next direction and the throughput factor that goes with it. Returns false when
// the sampled direction ends up below the surface, which is a path that has to end rather
// than a sample worth zero - a GGX lobe at grazing angles produces those regularly.
//
// Which lobe the coin landed on is no longer reported back: the ray reconstruction hit
// distance guide used to need it, and now measures itself with a probe ray that does not care
// what the path went on to do.
bool sampleBrdfDirection(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness,
  vec3 f0, float NdotV, vec2 xi, float lobeSelector,
  out vec3 L, out vec3 weight)
{
  float pSpecular = specularLobeProbability(albedo, f0, metallic, roughness);
  float alpha = roughness * roughness;

  if (lobeSelector < pSpecular)
  {
    // A mirror is not a narrow lobe, it is a different kind of object: its density is a delta
    // and there is nothing to importance sample. The direction is the reflection, and the
    // whole throughput is Fresnel - the delta cancels against the cosine and against its own
    // density, so no D, no G and no pdf survive the algebra. One ray, no variance, which is
    // what a mirror cost before path tracing existed and what it should cost here.
    if (alpha <= PT_DELTA_MAX_ALPHA)
    {
      L = reflect(-V, N);
      if (dot(N, L) <= 0.0)
        return false;

      // The half vector of a mirror is the normal, so the Fresnel angle is NdotV.
      weight = fresnelSchlick(NdotV, f0);
      weight /= pSpecular;
      return true;
    }

    // Plain half-vector GGX sampling through importanceSampleGGX from pbr.glsl - the same
    // sampler prefilter.frag bakes the environment with. Reusing it is the point: the
    // tracer's specular lobe cannot drift from the engine's own GGX convention, and it
    // already agrees with the D/G/F evaluateDirectLightSplit uses by construction. VNDF
    // sampling would cut variance at grazing angles, where this one wastes samples on
    // microfacets that face away; a second GGX sampler in the codebase is its price.
    // Nothing is floored: the branch above owns everything at or below the delta threshold,
    // so the alpha reaching the sampler is always wide enough to have a finite density.
    vec3 H = importanceSampleGGX(xi, N, roughness);
    recordNonFinite(H, PT_NF_GGX_H);
    L = reflect(-V, H);

    float NdotL = dot(N, L);
    float NdotH = dot(N, H);
    float VdotH = dot(V, H);
    // Recorded before the sign test rather than after it, because a NaN walks straight
    // through that test: all three comparisons are false, the || is false, and the function
    // goes on to build a weight out of it and return true.
    recordNonFinite(NdotL, PT_NF_GGX_NDOTL);
    recordNonFinite(NdotH, PT_NF_GGX_NDOTH);
    recordNonFinite(VdotH, PT_NF_GGX_VDOTH);
    if (NdotL <= 0.0 || NdotH <= 0.0 || VdotH <= 0.0)
      return false;

    // The GGX weight with the sampling pdf D * NdotH / (4 * VdotH) already cancelled: the
    // distribution term drops out entirely and what survives is F * G * VdotH / (NdotV *
    // NdotH). Verified against f * NdotL / pdf written out in full - they agree to machine
    // epsilon, so the cancellation itself is exact.
    //
    // k is alpha / 2, NOT the (roughness + 1)^2 / 8 that evaluateDirectLightSplit and every
    // raster shader use. Those two remaps are not interchangeable and picking the wrong one
    // here was a real bug: (roughness + 1)^2 / 8 is UE4's remap for ANALYTIC lights, where the
    // direction is handed to the BRDF, and it keeps k at 0.125 even as roughness goes to zero.
    // A sampled lobe on a smooth surface then loses most of its energy - measured in a white
    // furnace, a metal at NdotV 0.2 returned 0.41 where it owes 0.97 - and, worse, it does not
    // converge to Fresnel as roughness goes to zero, so it disagreed with the delta branch
    // above by a factor of 2.35 right at the handover. That step is what made the threshold
    // look load bearing. With alpha / 2 the two branches meet within a fraction of a percent
    // and the threshold stops deciding anything visible.
    float k = alpha * 0.5;
    float G = geometrySmith(k, NdotV, NdotL);
    recordNonFinite(G, PT_NF_GGX_G);
    vec3 F = fresnelSchlick(VdotH, f0);
    recordNonFinite(F, PT_NF_GGX_F);

    weight = F * G * VdotH / max(NdotV * NdotH, 1e-4);
    recordNonFinite(weight, PT_NF_GGX_WEIGHT);
    weight /= pSpecular;
    return true;
  }

  L = sampleCosineHemisphere(xi, N);
  if (dot(N, L) <= 0.0)
    return false;

  // Cosine-weighted sampling collapses the Lambert term to the albedo: the pdf is
  // NdotL / PI and the BRDF is kD * albedo / PI, so everything but kD * albedo cancels.
  // kD is built exactly as evaluateDirectLightSplit builds it.
  vec3 H = normalize(V + L);
  vec3 F = fresnelSchlick(max(dot(H, V), 0.0), f0);
  vec3 kD = (1.0 - F) * (1.0 - metallic);

  weight = kD * albedo;
  weight /= 1.0 - pSpecular;
  return true;
}

// The consumer's per-dispatch settings, handed in rather than read off its push constant block
// so a pass with a different block can run the same estimator.
struct PathSettings
{
  int maxBounces;
  // 0 = off, see clampContribution.
  float fireflyClamp;
  bool trackNonFinite;
};

// Task 1 and 2 diagnostics, live only while a PT debug mode is selected. Every one of them
// records what the estimator produced BEFORE clampContribution, because the question they answer
// is what the clamp is hiding.
struct PathDebug
{
  float maxContribution;
  float maxBounce;
  vec3 nee;
  vec3 environment;
};

// PROTOTYPE (dielectric reflection layer spike): what the starting vertex samples. A layer pixel
// runs the path twice from the same vertex, BASE then MIRROR - see pt_main.rgen.
const int PT_FIRST_VERTEX_COIN = 0;
// NEE and the diffuse lobe.
const int PT_FIRST_VERTEX_BASE = 1;
// The mirror lobe alone.
const int PT_FIRST_VERTEX_MIRROR = 2;

// The bounce loop from a vertex whose surface is already known: P and its geometric normal N,
// V pointing back along the ray that reached it. Contributions are added to radiance and
// pathDebug rather than returned, so a pixel that runs the path twice sums both runs.
void tracePath(vec3 P, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness,
  vec3 throughput, PathSettings settings, int firstVertex, inout uint rngState,
  inout vec3 radiance, inout PathDebug pathDebug)
{
  g_NonFiniteTracking = settings.trackNonFinite;

  // Bounded by the compile-time maximum so the loop is finite whatever maxBounces says; the
  // user's own budget breaks out below.
  for (int bounce = 0; bounce <= PT_MAX_BOUNCES; bounce++)
  {
    g_CurrentBounce = bounce;

    float NdotV = clamp(abs(dot(N, V)), 0.01, 0.99);
    vec3 f0 = mix(vec3(0.04), albedo, metallic);

    // Four draws off one stream: the BRDF sample, the lobe coin and the light. Every
    // vertex is treated the same, the first one included - see the sampling note above.
    vec2 brdfXi = randomFloat2(rngState);
    float lobeSelector = randomFloat(rngState);
    float lightSelector = randomFloat(rngState);

    // PROTOTYPE: the mirror pass gathers no NEE at its first vertex - a delta lobe sees no
    // delta light, and the diffuse half is the base pass's.
    if (firstVertex != PT_FIRST_VERTEX_MIRROR || bounce > 0)
    {
      vec3 directLight = estimateDirectLight(P, N, V, albedo, metallic, roughness, f0,
        NdotV, lightSelector);
      recordNonFinite(directLight, PT_NF_DIRECT_LIGHT);

      vec3 neeContribution = throughput * directLight;
      recordDebugContribution(neeContribution, bounce, pathDebug.maxContribution,
        pathDebug.maxBounce);
      pathDebug.nee += neeContribution;
      radiance += clampContribution(neeContribution, settings.fireflyClamp);
    }

    if (bounce >= settings.maxBounces)
      break;

    vec3 L;
    vec3 weight;
    if (firstVertex != PT_FIRST_VERTEX_COIN && bounce == 0)
    {
      // PROTOTYPE: forced lobe. A selector of 1 is never below the specular probability
      // (clamped to at most 0.9) and 0 always is; multiplying that probability back in
      // cancels the 1 / p the sampler divides by.
      float pSpecular = specularLobeProbability(albedo, f0, metallic, roughness);
      bool basePass = firstVertex == PT_FIRST_VERTEX_BASE;
      if (!sampleBrdfDirection(N, V, albedo, metallic, roughness, f0, NdotV, brdfXi,
        basePass ? 1.0 : 0.0, L, weight))
        break;
      weight *= basePass ? (1.0 - pSpecular) : pSpecular;
    }
    else if (!sampleBrdfDirection(N, V, albedo, metallic, roughness, f0, NdotV, brdfXi,
      lobeSelector, L, weight))
      break;

    // Only on the accepting path: a rejected sample leaves both outputs unwritten, and
    // reading them would report a non-finite value the estimator never used.
    recordNonFinite(weight, PT_NF_BRDF_WEIGHT);
    recordNonFinite(L, PT_NF_BRDF_L);

    throughput *= weight;
    recordNonFinite(throughput, PT_NF_THROUGHPUT);
    if (dot(throughput, throughput) <= 0.0)
      break;

    // Russian roulette from the third bounce on. Killing paths earlier costs more
    // variance than the traversal it saves, since the first bounces carry most of the
    // energy; the survival probability is the throughput itself, and dividing by it is
    // what keeps the estimator unbiased.
    if (bounce + 1 >= PT_RR_START_BOUNCE)
    {
      float survival = clamp(luminance(throughput), PT_RR_MIN_SURVIVAL, 1.0);
      if (randomFloat(rngState) > survival)
        break;
      throughput /= survival;
    }

    payload.hit = 0u;

    // No gl_RayFlagsOpaqueEXT: the flag forces every candidate opaque, and leaving it out is
    // what lets pathtrace.rahit run and cut alpha-tested geometry out of the path.
    traceRayEXT(u_Tlas, gl_RayFlagsNoneEXT, RT_MASK_OPAQUE,
      0u, // sbtRecordOffset: one hit group, and every instance names it
      0u, // sbtRecordStride: no per-geometry records to step over
      uint(PT_PRIMARY_MISS_INDEX),
      offsetRayOrigin(P, N), PT_RAY_TMIN, L, PT_RAY_TMAX,
      0); // payload location

    if (payload.hit == 0u)
    {
      // The environment is gathered here and only here - by BRDF sampling, never by
      // next event estimation. That is what keeps the two strategies disjoint and the
      // whole tracer free of MIS weights.
      vec3 environmentContribution = throughput * texture(u_Skybox, L).rgb;
      recordNonFinite(environmentContribution, PT_NF_ENVIRONMENT);
      recordDebugContribution(environmentContribution, bounce, pathDebug.maxContribution,
        pathDebug.maxBounce);
      pathDebug.environment += environmentContribution;
      radiance += clampContribution(environmentContribution, settings.fireflyClamp);
      break;
    }

    RayHitGeometry hit = resolveHitGeometry(payload.instanceIndex, payload.primitiveIndex,
      payload.objectToWorld, payload.barycentrics, L);

    PathSurface surface = resolveHitMaterial(hit, payload.barycentrics);

    if (surface.emissiveTexel)
    {
      // Same rule as the first vertex: an emissive texel is a pure emitter with no PBR
      // response left, so the path ends on it.
      vec3 emissiveContribution = throughput * surface.emissive;
      recordNonFinite(emissiveContribution, PT_NF_EMISSIVE);
      recordDebugContribution(emissiveContribution, bounce, pathDebug.maxContribution,
        pathDebug.maxBounce);
      radiance += clampContribution(emissiveContribution, settings.fireflyClamp);
      break;
    }

    // v1 shades against the geometric normal. An interpolated vertex normal, and the
    // material's normal map on top of it, belong here - hit already carries the vertex
    // stream and the attribute offset both of them would read. The geometric normal has
    // to survive that change anyway: the ray offset and the shadow ray need it.
    P = hit.position;
    N = hit.normal;
    V = -L;
    albedo = surface.albedo;
    metallic = surface.metallic;
    roughness = surface.roughness;
  }
}

#endif
