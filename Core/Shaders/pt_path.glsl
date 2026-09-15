#ifndef PT_PATH_GLSL
#define PT_PATH_GLSL

// The path estimator from an already known surface vertex on, shared so that everything that
// has to agree with the path traced reference runs the same math: pt_main.rgen starts it at the
// vertex its G-buffer describes, any other consumer wherever its own first ray landed.
//
// No version or extension directives here, for the reason raytracing_common.glsl gives. What a
// consumer owes this file:
//  - raytracing_common.glsl included under RT_BINDLESS, and light_eval.glsl and
//    Shared/EmissiveLightData.h included before u_Lights and the emissive light table are
//    declared, since their structs come from there;
//  - declared ahead of this file: u_Lights (LightBuffer), u_EmissiveHeader and u_EmissiveLights
//    (an EmissiveLightTableHeader followed by the EmissiveLightRecord array, in one std430
//    buffer), u_Skybox (samplerCube), payload (RayTracingPayload at location 0) and
//    shadowPayload (ShadowRayPayload at location 1);
//  - a shader binding table whose hit group record PT_PATH_HIT_GROUP is pathtrace.rchit +
//    pathtrace.rahit and record PT_SHADOW_HIT_GROUP pt_shadow.rahit alone, with pathtrace.rmiss at
//    miss index PT_PRIMARY_MISS_INDEX and pt_shadow.rmiss at PT_SHADOW_MISS_INDEX. A wrong order
//    still builds and runs, and silently zeroes next event estimation;
//  - its own NaN/Inf guard on the radiance it keeps: clampContribution drops a bad addend, but
//    nothing here promises the sum is finite.
// Non-finite tracking is armed only inside tracePath, from PathSettings, and g_NonFiniteCode is
// never reset between tracePath calls: with several paths in one invocation it names the first
// non-finite value of any of them.

#include "utils.glsl"
#include "pbr.glsl"
#include "random.glsl"
#include "light_eval.glsl"
#include "normal_map.glsl"
#include "dielectric.glsl"
#include "../Shared/PathTraceData.h"
#include "../Shared/EmissiveLightData.h"

const float PT_RAY_TMIN = 1e-3;
const float PT_RAY_TMAX = 100000.0;
// Multiplied by the magnitude of the coordinate, see rayOffsetDistance.
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

// How far offsetRayOrigin pushes a ray origin at this position: scaled with the magnitude of
// the coordinate so a point far from the world origin still gets an offset above the float
// spacing there.
float rayOffsetDistance(vec3 position)
{
  return PT_ORIGIN_OFFSET * max(1.0, max(abs(position.x), max(abs(position.y), abs(position.z))));
}

// Pushes a ray origin off the surface along the geometric normal. The first vertex needs it
// most: its position comes back through the depth buffer and carries the reconstruction error
// with it, not just the triangle's.
vec3 offsetRayOrigin(vec3 position, vec3 normal)
{
  return position + normal * rayOffsetDistance(position);
}

// A dielectric event pushes its ray origin a hundred times less, and its ray starts at a tmin of
// zero: glass walls are a few millimetres thick and liquids are modelled a fraction of one into
// them, while PT_ORIGIN_OFFSET scaled by a room-sized coordinate is a centimetre - enough to step
// clean over an interface. This still clears the float spacing of a hit position many times over.
const float PT_DIELECTRIC_ORIGIN_OFFSET = 1e-5;

vec3 offsetDielectricOrigin(vec3 position, vec3 normal)
{
  return position + normal
    * (PT_DIELECTRIC_ORIGIN_OFFSET * max(1.0, max(abs(position.x), max(abs(position.y), abs(position.z)))));
}

// sqrt of a value rounding can leave at or just below zero. sqrt itself is not trusted on an
// exact zero either, see importanceSampleGGX in pbr.glsl.
float safeSqrt(float x)
{
  return x > 0.0 ? sqrt(x) : 0.0;
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
  // True for a texel the G-buffer pass would have written as pure emission, seen from a face
  // that emits (emitsFromFace). It has given up its PBR response - so the path ends there,
  // exactly as the raster shading does.
  bool emissiveTexel;
  // The shading normal: the interpolated vertex normal bent by the normal map as gbuffer.frag
  // bends it, on the side of the geometric normal that faces the ray. Shading reads this one;
  // ray offsets, facing and the below-surface test keep RayHitGeometry::normal.
  vec3 normal;
};

// The per-texel decision the G-buffer pass makes: below the cutoff the emission is dropped and
// the texel stays PBR, above it the texel IS the emitter. A hit and an emissive light sample both
// decide through here, and both settle the face through emitsFromFace first, because next event
// estimation and a BSDF hit on the same point have to agree exactly or their MIS weights stop
// adding the emitter up to one.
bool resolveEmissiveTexel(RayTracingMaterialRecord material, vec2 texCoord, out vec3 emissive)
{
  emissive = vec3(0.0);
  if ((material.textureMask & RT_MATERIAL_EMISSIVE_SHADING) == 0u)
    return false;

  vec3 texel = material.emissivity;
  if ((material.textureMask & RT_MATERIAL_EMISSIVE_MAP) != 0u)
    texel *= textureLod(u_BindlessTextures[nonuniformEXT(material.emissiveIndex)],
      texCoord, 0.0).rgb;

  if (luminance(texel) <= EMISSIVE_SHADING_CUTOFF)
    return false;

  emissive = texel;
  return true;
}

PathSurface resolveHitMaterial(RayHitGeometry hit, vec2 barycentrics)
{
  PathSurface surface;
  surface.albedo = vec3(0.72);
  surface.metallic = 0.0;
  surface.roughness = 1.0;
  surface.emissive = vec3(0.0);
  surface.emissiveTexel = false;
  surface.normal = hit.shadingNormal;

  if (hit.materialIndex >= uint(u_Materials.length()))
    return surface;

  RayTracingMaterialRecord material = u_Materials[hit.materialIndex];

  // A dielectric is a smooth interface whatever its maps say. Of its textures only the normal map and
  // the emission still decide what a hit on it does, and without either no coordinate is needed.
  bool dielectric = (hit.flags & RT_INSTANCE_DIELECTRIC) != 0u;
  const uint dielectricMaps = RT_MATERIAL_NORMAL | RT_MATERIAL_EMISSIVE_MAP;
  vec2 texCoord = vec2(0.0);
  if (!dielectric || (material.textureMask & dielectricMaps) != 0u)
    texCoord = hitTexCoord(hit, barycentrics) * material.uvScale;

  if (!dielectric)
  {
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
  }

  // A mesh without tangents has no frame to decode the map in, and keeps the vertex normal.
  if ((material.textureMask & RT_MATERIAL_NORMAL) != 0u && dot(hit.tangent, hit.tangent) > 0.0)
  {
    vec3 tangentNormal = decodeNormalMap(
      textureLod(u_BindlessTextures[nonuniformEXT(material.normalIndex)], texCoord, 0.0).rgb,
      (material.textureMask & RT_MATERIAL_TWO_CHANNEL_NORMAL) != 0u ? 1.0 : 0.0);
    vec3 mapped = normalizeOrZero(mat3(hit.tangent, hit.bitangent, hit.vertexNormal) * tangentNormal);
    if (dot(mapped, mapped) > 0.0)
      surface.normal = dot(mapped, hit.normal) < 0.0 ? -mapped : mapped;
  }

  // The back of a single sided emitter emits nothing and keeps the PBR response resolved above,
  // so a path continues off it.
  if (emitsFromFace(hit.flags, hit.frontFace))
    surface.emissiveTexel = resolveEmissiveTexel(material, texCoord, surface.emissive);

  return surface;
}

// The consumer's per-dispatch settings, handed in rather than read off its push constant block
// so a pass with a different block can run the same estimator.
struct PathSettings
{
  int maxBounces;
  // 0 = off, see clampContribution.
  float fireflyClamp;
  bool trackNonFinite;
  // Refractive events the whole path may take on top of its bounces, and what happens once they are
  // spent - see solidTreatment.
  int maxTransmissionDepth;
  bool overflowStraight;
  // Whether a Solid refracts the rays on the camera's chain - every ray until the path shades a vertex
  // that is not a pure delta mirror (isPureDeltaMirror) - and every ray after one.
  bool chainRefract;
  bool secondaryRefract;
  // The PT Glass switch. Off, the TLAS holds no glass and nothing traces for it.
  bool glass;
};

// --- Glass crossed straight ---
//
// Sheet and ThinWalled glass never bends a ray, and a Solid need not: a segment through such glass is
// one line from end to end. Instead of a trace per interface it takes one trace to the next surface
// that is not crossed straight and one query for what every straight interface on the span lets
// through. The query reads each interface in pt_shadow.rahit and ignores them all, so neither their
// number nor their order changes the cost. What straight glass reflects is left out of a path, and what
// it emits reaches one through next event estimation alone.

// How a ray meets Solid glass: refracted, crossed straight, or as the end of the path.
const int PT_SOLID_REFRACT = 0;
const int PT_SOLID_STRAIGHT = 1;
const int PT_SOLID_TERMINATE = 2;

// From the ray's role - PathSettings::chainRefract or ::secondaryRefract - and the refractive events
// the path has already spent.
int solidTreatment(bool refractRole, int transmissionEvents, PathSettings settings)
{
  if (!refractRole)
    return PT_SOLID_STRAIGHT;
  if (transmissionEvents < settings.maxTransmissionDepth)
    return PT_SOLID_REFRACT;
  return settings.overflowStraight ? PT_SOLID_STRAIGHT : PT_SOLID_TERMINATE;
}

// A vertex that scatters along its mirror direction and nothing else, which the camera's chain
// continues through - the surfaces Primary Surface Replacement walks through.
bool isPureDeltaMirror(float roughness, float metallic)
{
  return roughness * roughness <= PT_DELTA_MAX_ALPHA && metallic >= PT_PSR_MIN_METALLIC;
}

// The glass a segment crosses straight under a Solid treatment.
uint straightGlassMask(int solidTreatment)
{
  return solidTreatment == PT_SOLID_STRAIGHT ? RT_MASK_GLASS : RT_MASK_GLASS_STRAIGHT;
}

// Per channel, what every glass interface of the classes in mask lets through between tMin and tMax
// along a ray: the TLAS leaves glass non-opaque, so the any-hit reads each one. crossedSolid reports
// whether one of the interfaces was the boundary of a solid medium.
vec3 traceStraightTransmittance(vec3 origin, vec3 direction, float tMin, float tMax, uint mask,
  out bool crossedSolid)
{
  shadowPayload.visible = 0u;
  shadowPayload.crossedSolid = 0u;
  shadowPayload.transmittance = vec3(1.0);
  shadowPayload.opticalDepth = vec3(0.0);

  traceRayEXT(u_Tlas, gl_RayFlagsSkipClosestHitShaderEXT, mask,
    uint(PT_SHADOW_HIT_GROUP), 0u, uint(PT_SHADOW_MISS_INDEX),
    origin, tMin, direction, tMax,
    1);

  crossedSolid = shadowPayload.crossedSolid != 0u;
  return shadowPayload.transmittance * exp(-max(shadowPayload.opticalDepth, vec3(0.0)));
}

// Finishes a segment whose closest hit over RT_MASK_PATH is in payload. A hit on glass the segment
// crosses straight is traced past, over everything else, to the next surface that counts, which
// replaces it in payload, and the straight glass on the span is queried. Returns that transmittance.
vec3 passStraightGlass(vec3 origin, vec3 direction, float tMin, int solidTreatment)
{
  if (payload.hit == 0u)
    return vec3(1.0);

  uint flags = u_Instances[payload.instanceIndex].flags;
  bool straight = (flags & RT_INSTANCE_STRAIGHT_DIELECTRIC) != 0u
    || (solidTreatment == PT_SOLID_STRAIGHT && (flags & RT_INSTANCE_SOLID_DIELECTRIC) != 0u);
  if (!straight)
    return vec3(1.0);

  // The same line from the same start, so nothing nearer than the glass can turn up.
  uint mask = straightGlassMask(solidTreatment);
  payload.hit = 0u;
  traceRayEXT(u_Tlas, gl_RayFlagsNoneEXT, RT_MASK_PATH & ~mask,
    uint(PT_PATH_HIT_GROUP), 0u, uint(PT_PRIMARY_MISS_INDEX),
    origin, tMin, direction, PT_RAY_TMAX,
    0);

  bool crossedSolid;
  return traceStraightTransmittance(origin, direction, tMin,
    payload.hit != 0u ? payload.hitT : PT_RAY_TMAX, mask, crossedSolid);
}

// One path segment: the surface it ends on, in payload, and what the glass it crosses straight on the
// way lets through. One trace where it meets no such glass, three where it meets any.
vec3 traceSegment(vec3 origin, vec3 direction, float tMin, int solidTreatment)
{
  payload.hit = 0u;

  // No gl_RayFlagsOpaqueEXT: the flag forces every candidate opaque, and leaving it out is what
  // lets pathtrace.rahit run and cut alpha-tested geometry out of the path.
  traceRayEXT(u_Tlas, gl_RayFlagsNoneEXT, RT_MASK_PATH,
    uint(PT_PATH_HIT_GROUP), // sbtRecordOffset: every instance names record 0
    0u, // sbtRecordStride: no per-geometry records to step over
    uint(PT_PRIMARY_MISS_INDEX),
    origin, tMin, direction, PT_RAY_TMAX,
    0); // payload location

  return passStraightGlass(origin, direction, tMin, solidTreatment);
}

// One shadow ray: how much of the light reaches its far end, per channel. TerminateOnFirstHit plus
// SkipClosestHitShader leaves the shadow miss shader as the only one that can say the light was
// reached. The shadow hit group's any-hit shapes the answer on the way: it is the alpha cutout -
// the reason this tracer gets foliage shadows right where the raster shadow atlas needs a separate
// pipeline variant for the same thing - and it attenuates the ray through every glass interface,
// crossed straight (see pt_shadow.rahit). Without glass - the PT Glass switch off, so the TLAS holds
// none - it traces the opaque geometry alone. crossedSolid reports whether one of the interfaces was the
// boundary of a solid medium.
vec3 traceShadowRay(vec3 origin, vec3 direction, float maxDistance, bool glass, out bool crossedSolid)
{
  shadowPayload.visible = 0u;
  shadowPayload.crossedSolid = 0u;
  shadowPayload.transmittance = vec3(1.0);
  shadowPayload.opticalDepth = vec3(0.0);

  traceRayEXT(u_Tlas,
    gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,
    glass ? RT_MASK_PATH : RT_MASK_OPAQUE,
    uint(PT_SHADOW_HIT_GROUP), // sbtRecordOffset: every instance names record 0
    0u, // sbtRecordStride: no per-geometry records to step over
    uint(PT_SHADOW_MISS_INDEX),
    origin, PT_RAY_TMIN, direction, maxDistance,
    1); // payload location

  crossedSolid = shadowPayload.crossedSolid != 0u;
  if (shadowPayload.visible == 0u)
    return vec3(0.0);

  return shadowPayload.transmittance * exp(-max(shadowPayload.opticalDepth, vec3(0.0)));
}

// --- Analytic lights ---

struct PathLightSample
{
  // Toward the light's centre until sampleAnalyticLightDirection replaces it.
  vec3 direction;
  // Attenuated and cone-shaped, shadowing excluded - that is the shadow ray's job.
  vec3 radiance;
  // Distance to the light's centre, PT_RAY_TMAX for the sun.
  float lightDistance;
  // Probability this light was the one picked, which the estimator divides back out.
  float selectionPdf;
  // Index into evaluateLightCandidate's flattened list.
  int candidate;
};

// Flattens the light buffer into one candidate list so the two passes below can walk it with
// the same code: candidate 0 is the directional light, then the point lights, then the spots.
// False means the light does not reach the point at all.
//
// A raster only light stands in for emissive geometry this tracer samples itself, so it reaches
// nothing here: a point or a spot is out of range, and the sun, which has no range, has nothing
// to give and drops out on the weight test.
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
    if ((u_Lights.directionalFlags & LIGHT_FLAG_RASTER_ONLY) != 0)
      radiance = vec3(0.0);
    lightDistance = PT_RAY_TMAX;
    return true;
  }

  int index = candidate - 1;
  if (index < pointCount)
  {
    if (u_Lights.pointLights[index].shadowPad.z > 0.5)
      return false;

    return evaluatePointLight(u_Lights.pointLights[index].positionRadius,
      u_Lights.pointLights[index].colorIntensity, worldPos, L, lightDistance, radiance);
  }

  index -= pointCount;
  if (u_Lights.spotLights[index].intensityShadow.w > 0.5)
    return false;

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
// Everything written here describes the light's centre; sampleAnalyticLightDirection turns the
// direction and the shadow distance into a point on the emitter afterwards.
bool selectLight(vec3 worldPos, float lightSelector, out PathLightSample result)
{
  int candidateCount = 1
    + min(u_Lights.pointLightCount, MAX_POINT_LIGHTS)
    + min(u_Lights.spotLightCount, MAX_SPOT_LIGHTS);

  result.direction = vec3(0.0, 1.0, 0.0);
  result.radiance = vec3(0.0);
  result.lightDistance = PT_RAY_TMAX;
  result.selectionPdf = 1.0;
  result.candidate = 0;

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
    result.candidate = candidate;
    chosenWeight = weight;

    cumulative += weight;
    if (cumulative > target)
      break;
  }

  result.selectionPdf = chosenWeight / totalWeight;
  return true;
}

// A uniformly distributed direction inside the cone around axis whose half angle has the given
// 1 - cos. Taking 1 - cos rather than cos is what keeps a half degree cone - the sun - from
// cancelling down to a handful of float steps. Also hands back the cosine and sine^2 of the
// sampled angle to the axis, off the same stable quantity.
vec3 sampleUniformCone(vec3 axis, float oneMinusCosMax, vec2 xi,
  out float cosTheta, out float sin2Theta)
{
  float oneMinusCos = xi.x * oneMinusCosMax;
  cosTheta = 1.0 - oneMinusCos;
  sin2Theta = max(oneMinusCos * (2.0 - oneMinusCos), 0.0);
  float sinTheta = safeSqrt(sin2Theta);
  float phi = 2.0 * PI * xi.y;

  // The tangent frame pbr.glsl's samplers build.
  vec3 up = abs(axis.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
  vec3 tangent = normalize(cross(up, axis));
  vec3 bitangent = cross(axis, tangent);

  return normalize(tangent * (cos(phi) * sinTheta) + bitangent * (sin(phi) * sinTheta)
    + axis * cosTheta);
}

// Gives the light selectLight picked its emitter size: a sphere of the source radius around a
// point or spot light, a disk of the angular radius for the sun. Next event estimation only -
// neither is geometry, so no bounce ray can hit one and there is nothing to weigh against.
//
// The direction is drawn uniformly inside the cone the emitter subtends. The radiance stays the
// one light_eval.glsl computed at the light's centre - falloff at the centre distance, spot cone
// from the centre direction - and the BRDF and receiver cosine are evaluated for the sampled
// direction. Spreading that radiance over the cone divides it by the cone's solid angle, and the
// uniform cone pdf is exactly one over that solid angle, so the two cancel: the size softens
// shadows and highlights without changing how much light arrives, and a size of zero is the delta
// light this used to be.
void sampleAnalyticLightDirection(inout PathLightSample light, vec3 worldPos, vec2 xi,
  out float shadowDistance)
{
  shadowDistance = light.lightDistance;

  float cosTheta;
  float sin2Theta;

  if (light.candidate == 0)
  {
    float angularRadius = u_Lights.directional.colorPad.w;
    if (angularRadius > 0.0)
    {
      // 1 - cos(r) written as 2 sin^2(r / 2).
      float halfSin = sin(0.5 * angularRadius);
      light.direction = sampleUniformCone(light.direction, 2.0 * halfSin * halfSin, xi,
        cosTheta, sin2Theta);
    }
    return;
  }

  int pointCount = min(u_Lights.pointLightCount, MAX_POINT_LIGHTS);
  int index = light.candidate - 1;
  float sourceRadius = index < pointCount
    ? u_Lights.pointLights[index].shadowPad.y
    : u_Lights.spotLights[index - pointCount].intensityShadow.z;

  // Inside or on the sphere there is no cone, and the direction to the centre stays.
  float centerDistance = light.lightDistance;
  if (sourceRadius <= 0.0 || centerDistance <= sourceRadius)
    return;

  // 1 - cos(theta max) as sin^2 / (1 + cos), which does not cancel for a small sphere.
  float sin2ThetaMax = (sourceRadius * sourceRadius) / (centerDistance * centerDistance);
  float cosThetaMax = safeSqrt(1.0 - sin2ThetaMax);
  light.direction = sampleUniformCone(light.direction, sin2ThetaMax / (1.0 + cosThetaMax), xi,
    cosTheta, sin2Theta);

  // The near intersection with the sphere along the sampled direction. Inside the cone the
  // discriminant is non-negative up to rounding.
  shadowDistance = centerDistance * cosTheta
    - safeSqrt(sourceRadius * sourceRadius - centerDistance * centerDistance * sin2Theta);
}

// --- Emissive lights ---
//
// Emitting geometry as a next event estimation light. TlasBuilder lists every instance whose
// material emits in the emissive light table, with a Vose alias table over a crude power estimate
// and this frame's object to world transform, which is what lets a moving emitter light the scene
// where it is now. A candidate is an instance drawn from the alias table, one of its triangles
// uniformly by index, and a uniform point on that triangle in world space.

// A uniform integer in [0, count), count above zero: a full 32 bit draw multiplied up and shifted
// down, where a float draw scaled by count would stop resolving past 2^24.
uint randomIndex(inout uint rngState, uint count)
{
  return uint((uint64_t(nextRandomUint(rngState)) * uint64_t(count)) >> 32);
}

struct EmissiveLightSample
{
  uint tableIndex;
  uint primitiveIndex;
  uvec3 triIndices;
  // In the payload's convention, so the texture coordinate interpolates exactly as for a hit.
  vec2 barycentrics;
  vec3 position;
  vec3 direction;
  // Solid angle density of drawing this point as one emissive candidate from the receiver.
  float sourcePdf;
};

// The solid angle density with which one emissive candidate lands on a point of the table's
// instance tableIndex: the instance's pmf, one triangle out of triangleCount, a uniform point over
// the triangle's world area, and area turned into solid angle at a receiver dist2 away that sees
// the triangle at cosEmitter. Both sides of the emitter MIS use this one formula.
float emissiveSourcePdf(uint tableIndex, float worldArea, float dist2, float cosEmitter)
{
  EmissiveLightRecord light = u_EmissiveLights[tableIndex];
  return light.pmf * dist2 / (float(light.triangleCount) * worldArea * cosEmitter);
}

// Draws one emissive candidate for the receiver at worldPos. False for a point worth nothing
// whatever it emits - a degenerate triangle, one the receiver sees exactly edge on, or the back face
// of a single sided instance (emitsFromFace, the rule a hit follows) - which the caller still counts
// as drawn: no resampling weight, never picked, no shadow ray.
bool sampleEmissiveLight(vec3 worldPos, inout uint rngState, out EmissiveLightSample result)
{
  uint slot = randomIndex(rngState, u_EmissiveHeader.count);
  result.tableIndex = randomFloat(rngState) < u_EmissiveLights[slot].aliasThreshold
    ? slot : u_EmissiveLights[slot].aliasIndex;

  EmissiveLightRecord light = u_EmissiveLights[result.tableIndex];
  RayTracingInstanceRecord instance = u_Instances[light.instanceIndex];

  result.primitiveIndex = randomIndex(rngState, light.triangleCount);
  result.triIndices = fetchTriangle(IndexStream(instance.indexAddress), result.primitiveIndex);

  VertexStream vertices = VertexStream(instance.vertexAddress);
  vec3 p0 = transformPointByRows(light.objectToWorld, fetchPosition(vertices, result.triIndices.x));
  vec3 p1 = transformPointByRows(light.objectToWorld, fetchPosition(vertices, result.triIndices.y));
  vec3 p2 = transformPointByRows(light.objectToWorld, fetchPosition(vertices, result.triIndices.z));

  // Uniform over the triangle: the square root makes the density grow with the area swept away
  // from the first vertex.
  vec2 xi = randomFloat2(rngState);
  float su = safeSqrt(xi.x);
  result.barycentrics = vec2(su * (1.0 - xi.y), su * xi.y);
  result.position = (1.0 - su) * p0 + result.barycentrics.x * p1 + result.barycentrics.y * p2;
  result.direction = vec3(0.0, 1.0, 0.0);
  result.sourcePdf = 0.0;

  vec3 crossed = cross(p1 - p0, p2 - p0);
  float doubleArea = length(crossed);
  vec3 toLight = result.position - worldPos;
  float dist2 = dot(toLight, toLight);
  if (doubleArea <= 0.0 || dist2 <= 0.0)
    return false;

  result.direction = toLight / sqrt(dist2);
  float cosEmitter = abs(dot(crossed, result.direction)) / doubleArea;
  if (cosEmitter <= 0.0)
    return false;

  // The density is the same whichever face the receiver sees - the facing decides what the point
  // emits, never how it was drawn - so a front face hit's MIS weights still add up to one.
  result.sourcePdf = emissiveSourcePdf(result.tableIndex, 0.5 * doubleArea, dist2, cosEmitter);
  return emitsFromFace(instance.flags, isRasterFrontFace(crossed, result.direction));
}

// The emission a candidate is weighed by before anything is fetched from a texture: the
// material's constant emissivity, as if its map were white and nothing were cut out. It is zero
// only where the true emission is zero too, which is all the resampling target owes the estimator.
vec3 emissiveTargetRadiance(uint tableIndex)
{
  uint materialIndex = u_Instances[u_EmissiveLights[tableIndex].instanceIndex].materialIndex;
  if (materialIndex >= uint(u_Materials.length()))
    return vec3(0.0);

  RayTracingMaterialRecord material = u_Materials[materialIndex];
  return (material.textureMask & RT_MATERIAL_EMISSIVE_SHADING) != 0u
    ? material.emissivity : vec3(0.0);
}

// What the chosen emissive candidate really emits, resolved exactly as resolveHitMaterial
// resolves a hit on the same point: the emissive map at the interpolated coordinate and the
// shading cutoff. A texel the alpha cutout removes does not exist for a ray, so it emits nothing
// here either - pathtrace.rahit only runs for the alpha tested instances the TLAS leaves
// non-opaque, hence the flag test.
vec3 resolveEmissiveSampleRadiance(EmissiveLightSample emitter)
{
  RayTracingInstanceRecord instance = u_Instances[u_EmissiveLights[emitter.tableIndex].instanceIndex];
  if (instance.materialIndex >= uint(u_Materials.length()))
    return vec3(0.0);

  if ((instance.flags & RT_INSTANCE_ALPHA_TEST) != 0u
    && isAlphaCutout(instance, emitter.primitiveIndex, emitter.barycentrics))
    return vec3(0.0);

  RayTracingMaterialRecord material = u_Materials[instance.materialIndex];

  // hitTexCoord's rule: no attribute block, no coordinate.
  vec2 texCoord = vec2(0.0);
  if (instance.attributeOffset != 0u)
    texCoord = interpolateTexCoord(VertexStream(instance.vertexAddress), instance.attributeOffset,
      emitter.triIndices, emitter.barycentrics);

  vec3 emissive;
  resolveEmissiveTexel(material, texCoord * material.uvScale, emissive);
  return emissive;
}

// --- BRDF sampling ---

// The Smith-Schlick k of every BRDF evaluation the path tracer makes: the GGX bounce weight in
// sampleBrdfDirection and next event estimation's evaluateNeeResponse alike, so both strategies
// of the emitter MIS weigh the identical BRDF. sampleBrdfDirection explains why it is alpha / 2
// rather than the (roughness + 1)^2 / 8 raster shading keeps.
float pathSmithK(float alpha)
{
  return alpha * 0.5;
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
// deltaLobe reports whether the direction came from the delta mirror branch. The emitter MIS
// needs it: no light sample can reproduce a mirror direction, so an emitter hit through one keeps
// its full weight. The ray reconstruction hit distance guide does not use it - it measures itself
// with a probe ray that does not care what the path went on to do.
bool sampleBrdfDirection(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness,
  vec3 f0, float NdotV, vec2 xi, float lobeSelector,
  out vec3 L, out vec3 weight, out bool deltaLobe)
{
  float pSpecular = specularLobeProbability(albedo, f0, metallic, roughness);
  float alpha = roughness * roughness;
  deltaLobe = false;

  if (lobeSelector < pSpecular)
  {
    // A mirror is not a narrow lobe, it is a different kind of object: its density is a delta
    // and there is nothing to importance sample. The direction is the reflection, and the
    // whole throughput is Fresnel - the delta cancels against the cosine and against its own
    // density, so no D, no G and no pdf survive the algebra. One ray, no variance, which is
    // what a mirror cost before path tracing existed and what it should cost here.
    if (alpha <= PT_DELTA_MAX_ALPHA)
    {
      deltaLobe = true;
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
    // agrees with the D and F of evaluateDirectLightSplitSmithK by construction. VNDF
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
    // k is alpha / 2 (pathSmithK), NOT the (roughness + 1)^2 / 8 that evaluateDirectLightSplit and
    // every raster shader use. Those two remaps are not interchangeable and picking the wrong one
    // here was a real bug: (roughness + 1)^2 / 8 is UE4's remap for ANALYTIC lights, where the
    // direction is handed to the BRDF, and it keeps k at 0.125 even as roughness goes to zero.
    // A sampled lobe on a smooth surface then loses most of its energy - measured in a white
    // furnace, a metal at NdotV 0.2 returned 0.41 where it owes 0.97 - and, worse, it does not
    // converge to Fresnel as roughness goes to zero, so it disagreed with the delta branch
    // above by a factor of 2.35 right at the handover. That step is what made the threshold
    // look load bearing. With alpha / 2 the two branches meet within a fraction of a percent
    // and the threshold stops deciding anything visible. Next event estimation evaluates its
    // BRDF with the same pathSmithK, so it and this sampler weigh one BRDF; raster keeps its remap.
    float k = pathSmithK(alpha);
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

// What the starting vertex samples. A reflection layer pixel runs the path twice from the same
// vertex, BASE then MIRROR - see pt_main.rgen.
const int PT_FIRST_VERTEX_COIN = 0;
// NEE and the diffuse lobe.
const int PT_FIRST_VERTEX_BASE = 1;
// The mirror lobe alone.
const int PT_FIRST_VERTEX_MIRROR = 2;

// The solid angle density with which sampleBrdfDirection, driven the way tracePath drives it at
// this vertex, generates L. It is the BSDF half of every emitter MIS weight, so it follows the
// sampler exactly: the lobe coin's mixture of the half vector GGX density and the cosine density,
// with the same lobe probability and the same rejections. A delta mirror lobe has no density to
// share and adds nothing; the sampler flags a direction it produces instead. A forced first
// vertex lobe (lobeMode PT_FIRST_VERTEX_BASE or _MIRROR) is sampled with that lobe's own
// density, and nothing else.
float brdfDirectionPdf(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness,
  vec3 f0, int lobeMode)
{
  float NdotL = dot(N, L);
  if (NdotL <= 0.0)
    return 0.0;

  float diffusePdf = NdotL / PI;

  float specularPdf = 0.0;
  float alpha = roughness * roughness;
  if (alpha > PT_DELTA_MAX_ALPHA)
  {
    vec3 H = normalize(V + L);
    float NdotH = dot(N, H);
    float VdotH = dot(V, H);
    if (NdotH > 0.0 && VdotH > 0.0)
    {
      // The GGX distribution importanceSampleGGX draws from, with sin^2 taken off a cross
      // product: normalDistributionGGX floors its denominator, which just above the delta
      // threshold is orders of magnitude off the density the sampler really has, and 1 - cos^2
      // cancels away there.
      vec3 axis = cross(N, H);
      float alpha2 = alpha * alpha;
      float denominator = dot(axis, axis) + alpha2 * NdotH * NdotH;
      float D = alpha2 / (PI * denominator * denominator);
      // The half vector density D * NdotH through the reflection's Jacobian 1 / (4 VdotH).
      specularPdf = D * NdotH / (4.0 * VdotH);
    }
  }

  if (lobeMode == PT_FIRST_VERTEX_BASE)
    return diffusePdf;
  if (lobeMode == PT_FIRST_VERTEX_MIRROR)
    return specularPdf;

  float pSpecular = specularLobeProbability(albedo, f0, metallic, roughness);
  return pSpecular * specularPdf + (1.0 - pSpecular) * diffusePdf;
}

// The power heuristic weight of the strategy that drew a sample with density a, against one that
// could have drawn it with density b. A ratio rather than squares, so a density too large to
// square - a tiny or grazing emitter triangle - still gives a finite weight. The two sides' weights
// add up to one.
float powerHeuristic(float a, float b)
{
  if (b <= 0.0)
    return 1.0;
  if (a <= 0.0)
    return 0.0;

  float ratio = b / a;
  return 1.0 / (1.0 + ratio * ratio);
}

// --- Next event estimation ---

// What next event estimation gets from radiance arriving along L: pbr.glsl's D, F and kD split that
// deferred_lighting.frag evaluates per light, but with the path tracer's Smith-Schlick k
// (pathSmithK) in place of the raster remap, so a light sample and a BSDF sample of one direction
// weigh the identical BRDF - which the emitter MIS weights assume - times the receiver cosine. The
// diffuse half does not depend on k and still matches the raster exactly.
//
// A delta lobe keeps the diffuse half only; see PT_DELTA_MAX_ALPHA. No light sample can land in
// a mirror direction, so the specular term there is not small but identically nothing - the split
// evaluation exists so the diffuse half a dielectric mirror still has survives. A sphere light or
// the sun disk is therefore not reflected in a mirror at all; emitting geometry is, through the
// mirror bounce hitting it. The raster path needs no such branch: it never samples the lobe, so
// its narrow highlight is the intended stand-in for a light that has no area.
vec3 evaluateNeeResponse(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metallic,
  float roughness, vec3 f0, float NdotV)
{
  float alpha = roughness * roughness;

  vec3 diffuse;
  vec3 specular;
  vec3 total = evaluateDirectLightSplitSmithK(N, V, L, radiance, albedo, metallic, alpha,
    pathSmithK(alpha), f0, NdotV, diffuse, specular);

  return alpha <= PT_DELTA_MAX_ALPHA ? diffuse : total;
}

// Next event estimation at one path vertex: candidates from two kinds of light resampled down to
// one, and ONE shadow ray. Stateless resampled importance sampling - nothing survives the vertex,
// so no sample is reused across pixels or frames and the white noise the ray reconstruction guide
// asks for is kept.
//  - The analytic kind draws one candidate: selectLight, then its emitter size.
//  - The emissive kind draws PT_EMISSIVE_CANDIDATES points on emitting geometry.
// The kinds sample disjoint light sources. Every candidate carries g, its own unshadowed
// one-sample estimate already divided by the density it was drawn with, and is picked with a
// probability proportional to luminance(g) over the number of candidates its kind drew - that
// count is the whole balance weight when supports are disjoint. The pick is unbiased once its
// value is scaled by sum(w) / luminance(g). A candidate that evaluates to zero was still drawn and
// still counts.
//
// An emissive candidate is weighed by its material's constant emissivity; only the one picked
// fetches its true emission. It then shares its emitter with the bounce ray that could hit the same
// point, by the power heuristic over emissiveSourcePdf and brdfDirectionPdf - tracePath applies
// the other half. The analytic lights need no MIS weight, since no bounce ray can hit them, and
// neither does the environment: only a bounce ray that misses collects it, which next event
// estimation never samples.
//
// N shades; Ng, the geometric normal, offsets the shadow ray and rejects light from below the
// surface. solidBlocksBounce says a bounce ray from here would not cross a Solid straight. glass is
// PathSettings::glass.
vec3 estimateDirectLight(vec3 worldPos, vec3 N, vec3 Ng, vec3 V, vec3 albedo, float metallic,
  float roughness, vec3 f0, float NdotV, int lobeMode, bool solidBlocksBounce, bool glass,
  inout uint rngState)
{
  float weightSum = 0.0;
  bool chosenEmissive = false;
  // The analytic candidate's estimate, or an emissive candidate's per unit of emitted radiance.
  vec3 chosenEstimate = vec3(0.0);
  float chosenTarget = 0.0;
  vec3 chosenDirection = vec3(0.0, 1.0, 0.0);
  float chosenShadowDistance = 0.0;
  EmissiveLightSample chosenEmitter;

  float lightSelector = randomFloat(rngState);
  vec2 lightXi = randomFloat2(rngState);
  PathLightSample light;
  if (selectLight(worldPos, lightSelector, light))
  {
    float shadowDistance;
    sampleAnalyticLightDirection(light, worldPos, lightXi, shadowDistance);

    // Backfacing directions cost nothing but the test - no shadow ray is traced for them.
    vec3 estimate = vec3(0.0);
    if (dot(N, light.direction) > 0.0 && dot(Ng, light.direction) > 0.0)
      estimate = evaluateNeeResponse(N, V, light.direction, light.radiance, albedo, metallic,
        roughness, f0, NdotV) / light.selectionPdf;

    // The one analytic candidate, so its weight is divided by one.
    float target = max(luminance(estimate), 0.0);
    weightSum += target;
    if (target > 0.0 && randomFloat(rngState) * weightSum < target)
    {
      chosenEmissive = false;
      chosenEstimate = estimate;
      chosenTarget = target;
      chosenDirection = light.direction;
      chosenShadowDistance = shadowDistance;
    }
  }

  if (u_EmissiveHeader.count > 0u)
  {
    for (int candidate = 0; candidate < PT_EMISSIVE_CANDIDATES; candidate++)
    {
      EmissiveLightSample emitter;
      if (!sampleEmissiveLight(worldPos, rngState, emitter))
        continue;
      if (dot(N, emitter.direction) <= 0.0 || dot(Ng, emitter.direction) <= 0.0)
        continue;

      vec3 response = evaluateNeeResponse(N, V, emitter.direction, vec3(1.0), albedo, metallic,
        roughness, f0, NdotV) / emitter.sourcePdf;
      float target = max(luminance(response * emissiveTargetRadiance(emitter.tableIndex)), 0.0);
      float weight = target / float(PT_EMISSIVE_CANDIDATES);

      weightSum += weight;
      if (weight > 0.0 && randomFloat(rngState) * weightSum < weight)
      {
        chosenEmissive = true;
        chosenEstimate = response;
        chosenTarget = target;
        chosenDirection = emitter.direction;
        chosenEmitter = emitter;
      }
    }
  }

  if (weightSum <= 0.0)
    return vec3(0.0);

  vec3 value = chosenEstimate;
  vec3 shadowOrigin = offsetRayOrigin(worldPos, Ng);
  vec3 shadowDirection = chosenDirection;
  float shadowDistance = chosenShadowDistance;

  if (chosenEmissive)
  {
    value *= resolveEmissiveSampleRadiance(chosenEmitter);

    // Aimed at the sample from the offset origin itself and stopped short of it by the offset a
    // ray origin gets there, so the triangle the point lies on cannot shadow its own sample.
    vec3 toEmitter = chosenEmitter.position - shadowOrigin;
    float emitterDistance = length(toEmitter);
    shadowDistance = emitterDistance - rayOffsetDistance(chosenEmitter.position);
    if (emitterDistance > 0.0)
      shadowDirection = toEmitter / emitterDistance;
  }

  if (max(value.r, max(value.g, value.b)) <= 0.0)
    return vec3(0.0);

  // Nothing fits between a receiver and a light nearer than the ray's own start.
  bool crossedSolid = false;
  vec3 visibility = shadowDistance > PT_RAY_TMIN
    ? traceShadowRay(shadowOrigin, shadowDirection, shadowDistance, glass, crossedSolid)
    : vec3(1.0);
  if (max(visibility.r, max(visibility.g, visibility.b)) <= 0.0)
    return vec3(0.0);

  // The shadow ray crosses every glass straight, and so does a bounce ray from here unless it would
  // refract at a Solid on the way - or end there. Then no bounce reaches an emitter along this line,
  // followPathRay gives any emitter it reaches through a Solid boundary weight zero, and the shadow ray
  // is the only strategy for that light, weight one. So it is for an emitter that is itself glass the
  // bounce ray would cross straight - Sheet and ThinWalled always, a Solid unless the bounce refracts at
  // or ends on it: traceSegment passes such glass without a hit, so no bounce sees what it emits.
  float misWeight = 1.0;
  if (chosenEmissive && !(crossedSolid && solidBlocksBounce)
    && (u_EmissiveLights[chosenEmitter.tableIndex].flags & EMISSIVE_LIGHT_NEE_ONLY) == 0u)
  {
    uint emitterFlags = u_Instances[u_EmissiveLights[chosenEmitter.tableIndex].instanceIndex].flags;
    bool bounceCrossesEmitter = (emitterFlags & RT_INSTANCE_STRAIGHT_DIELECTRIC) != 0u
      || (!solidBlocksBounce && (emitterFlags & RT_INSTANCE_SOLID_DIELECTRIC) != 0u);
    if (!bounceCrossesEmitter)
    {
      misWeight = powerHeuristic(chosenEmitter.sourcePdf,
        brdfDirectionPdf(N, V, chosenDirection, albedo, metallic, roughness, f0, lobeMode));
    }
  }

  return value * (weightSum / chosenTarget) * (visibility * misWeight);
}

// --- The path ---

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

// --- Smooth dielectrics ---
//
// A surface whose instance is flagged RT_INSTANCE_*_DIELECTRIC is a perfectly smooth dielectric: a
// delta interface with an IOR and absorption, whatever albedo, metallic and roughness its material
// carries for raster. It gathers no next event estimation - a delta BSDF sees no light sample.
//
// Sheet: single-layer geometry with no inside. One hit is a whole slab: it reflects with the slab
// reflectance and lets the rest through unbent, tinted by transmittanceTint.
//
// ThinWalled: one wall of a closed thin vessel. One hit is one interface, crossed unbent, and the
// absorption of the walls comes from the query's signed distances, never from the medium stack.
//
// Sheet and ThinWalled glass is always crossed straight on a path (traceSegment). Only the camera's
// front-most glass surface reflects it, into the reflection layer - see pt_main.rgen.
//
// Solid: the boundary of a medium, entered through the front face and left through the back one by
// object space winding (RayTracingPayload::frontFacing). Whether a ray refracts at it or crosses it
// straight is the ray's role and the budget left (solidTreatment). A refracting ray scatters by the
// exact Fresnel coin: reflection with probability F, transmission otherwise, weight one either way.
// Its events spend the path's transmission budget instead of its bounces, and no Russian roulette
// runs at them. Snell's law, total internal reflection where it has no answer, and Beer-Lambert
// absorption over every segment travelled inside. Radiance is not scaled by eta^2 at the interfaces:
// it is eta^2 on the way in and 1 / eta^2 on the way out, and every path that ends on the camera or an
// emitter crosses them in pairs.
//
// Overlapping media follow Schmidt & Budge, "Simple Nested Dielectrics in Ray Traced Images" (2002):
// every Solid material has a priority, the path keeps the media it is inside on a small stack, and
// the boundary of a medium inside one of a higher priority is no interface at all - the path goes
// through it untouched. That is what lets a liquid be modelled into its glass wall: the liquid owns
// the overlap and the glass surface in there does not exist. A real interface bends by the current
// medium's IOR over the one beyond it.

// The media a path is inside, as material slots, in the order it entered them. The current medium
// is the entry of the highest priority, the latest one among equals; none means air. Only refraction
// enters one: a segment that crosses Solid glass straight empties the stack, since the query's signed
// distances then account for the absorption up to where the ray leaves.
struct MediumStack
{
  uint materials[PT_MEDIUM_STACK_SIZE];
  int count;
};

MediumStack emptyMediumStack()
{
  MediumStack stack;
  for (int i = 0; i < PT_MEDIUM_STACK_SIZE; i++)
    stack.materials[i] = 0u;
  stack.count = 0;
  return stack;
}

// The slot of the current medium, -1 in air. exclude leaves one slot out, which is what answers
// what lies beyond a medium the path is about to leave.
int currentMediumSlot(MediumStack stack, int exclude)
{
  int slot = -1;
  int priority = 0;
  for (int i = 0; i < stack.count; i++)
  {
    if (i == exclude)
      continue;

    int candidate = u_Materials[stack.materials[i]].mediumPriority;
    if (slot < 0 || candidate >= priority)
    {
      slot = i;
      priority = candidate;
    }
  }
  return slot;
}

float mediumIor(MediumStack stack, int slot)
{
  return slot < 0 ? 1.0 : u_Materials[stack.materials[slot]].ior;
}

vec3 mediumAbsorption(MediumStack stack)
{
  int slot = currentMediumSlot(stack, -1);
  return slot < 0 ? vec3(0.0) : u_Materials[stack.materials[slot]].absorption;
}

int findMedium(MediumStack stack, uint materialIndex)
{
  for (int i = stack.count - 1; i >= 0; i--)
  {
    if (stack.materials[i] == materialIndex)
      return i;
  }
  return -1;
}

// A path nested deeper than the stack forgets the newest medium; leaving it later reads as leaving
// a medium it never entered.
void pushMedium(inout MediumStack stack, uint materialIndex)
{
  if (stack.count >= PT_MEDIUM_STACK_SIZE)
    return;

  stack.materials[stack.count] = materialIndex;
  stack.count++;
}

void removeMedium(inout MediumStack stack, int slot)
{
  if (slot < 0 || slot >= stack.count)
    return;

  for (int i = slot; i < stack.count - 1; i++)
    stack.materials[i] = stack.materials[i + 1];
  stack.count--;
}

// One dielectric interface as the path meets it.
struct DielectricInterface
{
  // False for the boundary of a medium inside one of a higher priority, which the path passes
  // straight through with no Fresnel.
  bool real;
  // Sheet: one hit is a whole slab.
  bool sheet;
  // Sheet or ThinWalled: crossed unbent, into no medium.
  bool straight;
  // Solid: through the front face, into the medium.
  bool entering;
  // n on the incoming side over n on the far side.
  float eta;
  // Solid, leaving: the stack slot of the medium left, -1 when the stack does not hold it.
  int leavingSlot;
  // Sheet: the tint of the crossing.
  vec3 tint;
};

DielectricInterface resolveDielectricInterface(MediumStack stack, uint materialIndex, uint flags,
  bool frontFacing)
{
  RayTracingMaterialRecord material = u_Materials[materialIndex];

  DielectricInterface face;
  face.real = true;
  face.sheet = (flags & RT_INSTANCE_SHEET_DIELECTRIC) != 0u;
  face.straight = (flags & RT_INSTANCE_STRAIGHT_DIELECTRIC) != 0u;
  face.entering = frontFacing;
  face.leavingSlot = -1;
  face.tint = material.transmittanceTint;

  int current = currentMediumSlot(stack, -1);
  float currentIor = mediumIor(stack, current);

  // Straight glass has no inside the path keeps: whichever way it is crossed, the medium around it is
  // on both sides.
  if (face.straight || frontFacing)
  {
    face.real = face.straight || current < 0
      || u_Materials[stack.materials[current]].mediumPriority <= material.mediumPriority;
    face.eta = currentIor / material.ior;
    return face;
  }

  face.leavingSlot = findMedium(stack, materialIndex);
  if (face.leavingSlot < 0)
  {
    // Leaving a medium the path never entered - it started inside, or the stack overflowed:
    // into whatever the path is in now.
    face.eta = material.ior / currentIor;
    return face;
  }

  face.real = current == face.leavingSlot
    || u_Materials[stack.materials[current]].mediumPriority <= material.mediumPriority;
  face.eta = material.ior / mediumIor(stack, currentMediumSlot(stack, face.leavingSlot));
  return face;
}

// PT_DIELECTRIC_* is what decides the event: the Fresnel coin, or one outcome forced - which keeps
// its expected weight, F or its complement, instead of the 1 / p a pick would pay.
const int PT_DIELECTRIC_COIN = 0;
const int PT_DIELECTRIC_TRANSMIT = 1;
const int PT_DIELECTRIC_REFLECT = 2;

struct DielectricScatter
{
  vec3 origin;
  vec3 direction;
  // Into the throughput.
  vec3 weight;
  bool reflected;
  // Through the boundary of a solid medium, real or not.
  bool crossedSolid;
};

// Continues a ray that arrived along direction at a dielectric, and updates the medium stack for the
// transmission it decides on. False when the path ends there: a forced outcome with nothing to carry,
// or a new direction on the wrong side of the geometric surface. N is the shading normal.
bool scatterDielectric(RayHitGeometry hit, vec3 N, vec3 direction, DielectricInterface face,
  int event, float selector, uint materialIndex, inout MediumStack media,
  out DielectricScatter result)
{
  result.origin = hit.position;
  result.direction = direction;
  result.weight = vec3(1.0);
  result.reflected = false;
  result.crossedSolid = false;

  // A shading normal the ray arrives from behind has no angle to scatter at; the geometric
  // normal stands in for it.
  vec3 normal = dot(N, -direction) > 0.0 ? N : hit.normal;
  float cosIncident = clamp(dot(normal, -direction), 0.0, 1.0);

  float reflectance = 0.0;
  float cosTransmitted = 1.0;
  if (face.real)
  {
    reflectance = face.sheet
      ? thinSlabReflectance(cosIncident, face.eta)
      : fresnelDielectric(cosIncident, face.eta, cosTransmitted);
  }

  bool reflects = event == PT_DIELECTRIC_REFLECT
    || (event == PT_DIELECTRIC_COIN && selector < reflectance);

  if (reflects)
  {
    if (event == PT_DIELECTRIC_REFLECT)
      result.weight = vec3(reflectance);
    if (reflectance <= 0.0)
      return false;

    result.reflected = true;
    result.direction = reflect(direction, normal);
    if (dot(result.direction, hit.normal) <= 0.0)
      return false;

    result.origin = offsetDielectricOrigin(hit.position, hit.normal);
    return true;
  }

  if (event == PT_DIELECTRIC_TRANSMIT)
    result.weight = vec3(1.0 - reflectance);
  // Total internal reflection leaves nothing to transmit.
  if (reflectance >= 1.0)
    return false;

  if (face.straight)
  {
    // A ThinWalled wall's absorption belongs to a span, which only a query knows.
    if (face.sheet)
      result.weight *= face.tint;
  }
  else
  {
    result.crossedSolid = true;
    // refract(), written out with the cosine fresnelDielectric already has.
    if (face.real)
      result.direction = normalize(face.eta * direction + (face.eta * cosIncident - cosTransmitted) * normal);
  }

  if (dot(result.direction, hit.normal) >= 0.0)
    return false;

  result.origin = offsetDielectricOrigin(hit.position, -hit.normal);

  if (!face.straight)
  {
    if (face.entering)
      pushMedium(media, materialIndex);
    else
      removeMedium(media, face.leavingSlot);
  }

  return true;
}

// The vertex a ray left from, as far as the emitter MIS weight at the far end needs it.
struct PathNeeVertex
{
  // Whether it gathered next event estimation at all.
  bool gathered;
  // Whether the ray left it along a delta lobe, which no light sample reproduces.
  bool delta;
  vec3 P;
  vec3 N;
  vec3 V;
  vec3 albedo;
  float metallic;
  float roughness;
  vec3 f0;
  int lobeMode;
};

// Follows one ray through every dielectric it meets to the surface the path shades next, and returns
// true with it. The sky on a miss and an emitter on a hit are added here and end the path, as does a
// Solid the path may no longer refract at under PT_GLASS_OVERFLOW_TERMINATE, or a dielectric event
// that goes nowhere. onChain is the role the rays of this walk have, see PathSettings::chainRefract.
//
// The emitter MIS weight follows the ray across its dielectrics. Glass crossed straight changes
// nothing: the NEE vertex's shadow ray could have drawn the emitter along the same line and
// pt_shadow.rahit attenuates it by the same interfaces, so the power heuristic applies as if they were
// not there. A transmission through a solid medium's boundary - a refraction, or a boundary a
// higher-priority medium owns - gives weight zero: the shadow ray through it already counted that light,
// straight and filtered, at weight one - see estimateDirectLight. An emitter that is glass crossed straight
// is never a hit here at all. A
// reflection off a dielectric on the way is a delta the shadow ray cannot reproduce, and keeps weight
// one, like everything after a vertex that gathered no NEE.
bool followPathRay(vec3 origin, vec3 direction, float tMin, PathNeeVertex nee,
  PathSettings settings, int bounce, bool onChain, inout vec3 throughput, inout int transmissionEvents,
  inout MediumStack media, inout uint rngState, inout vec3 radiance, inout PathDebug pathDebug,
  out RayHitGeometry hit, out PathSurface surface, out vec3 incoming)
{
  bool refractRole = onChain ? settings.chainRefract : settings.secondaryRefract;
  bool reflectedOnWay = false;
  bool refracted = false;

  // One segment per refractive event plus the one that ends the walk; the budget ends it first.
  for (int segment = 0; segment <= PT_MAX_TRANSMISSION_DEPTH; segment++)
  {
    int solid = solidTreatment(refractRole, transmissionEvents, settings);
    if (solid == PT_SOLID_STRAIGHT && media.count > 0)
      media = emptyMediumStack();

    throughput *= traceSegment(origin, direction, tMin, solid);

    if (payload.hit == 0u)
    {
      // The environment is gathered here and only here - by BRDF sampling, never by
      // next event estimation. That is what keeps the two strategies disjoint for it and
      // leaves the sky without an MIS weight.
      vec3 environmentContribution = throughput * texture(u_Skybox, direction).rgb;
      recordNonFinite(environmentContribution, PT_NF_ENVIRONMENT);
      recordDebugContribution(environmentContribution, bounce, pathDebug.maxContribution,
        pathDebug.maxBounce);
      pathDebug.environment += environmentContribution;
      radiance += clampContribution(environmentContribution, settings.fireflyClamp);
      return false;
    }

    // Beer-Lambert over the segment that just ended, in the medium it ran through.
    throughput *= exp(-mediumAbsorption(media) * payload.hitT);

    hit = resolveHitGeometry(payload.instanceIndex, payload.primitiveIndex,
      payload.objectToWorld, payload.barycentrics, direction);
    surface = resolveHitMaterial(hit, payload.barycentrics);
    incoming = direction;

    // An emissive texel is an emitter first, whatever else its instance is. Any other dielectric
    // still here is a Solid the segment does not cross straight.
    if (!surface.emissiveTexel && (hit.flags & RT_INSTANCE_DIELECTRIC) != 0u
      && hit.materialIndex < uint(u_Materials.length()))
    {
      if (solid != PT_SOLID_REFRACT)
        return false;
      transmissionEvents++;

      DielectricInterface face = resolveDielectricInterface(media, hit.materialIndex, hit.flags,
        payload.frontFacing != 0u);
      DielectricScatter scatter;
      if (!scatterDielectric(hit, surface.normal, direction, face, PT_DIELECTRIC_COIN,
        randomFloat(rngState), hit.materialIndex, media, scatter))
        return false;

      throughput *= scatter.weight;
      recordNonFinite(throughput, PT_NF_THROUGHPUT);
      reflectedOnWay = reflectedOnWay || scatter.reflected;
      refracted = refracted || scatter.crossedSolid;
      origin = scatter.origin;
      direction = scatter.direction;
      tMin = 0.0;
      continue;
    }

    if (surface.emissiveTexel)
    {
      // Same rule as the first vertex: an emissive texel is a pure emitter with no PBR
      // response left, so the path ends on it.
      //
      // Next event estimation at the vertex this ray left could have drawn the same point, so the
      // two share it by the power heuristic, over the very densities that side uses. The hit keeps
      // its full weight where nothing competed for it: after a mirror bounce, which no light sample
      // reproduces, after a vertex that gathered no NEE, and on an emitter the table does not hold.
      float misWeight = 1.0;
      uint emissiveIndex = u_Instances[payload.instanceIndex].emissiveIndex;
      if (nee.gathered && !nee.delta && !reflectedOnWay && emissiveIndex < u_EmissiveHeader.count)
      {
        vec3 toHit = hit.position - nee.P;
        float cosEmitter = abs(dot(hit.normal, direction));
        if ((u_EmissiveLights[emissiveIndex].flags & EMISSIVE_LIGHT_NEE_ONLY) == 0u
          && hit.area > 0.0 && cosEmitter > 0.0)
        {
          misWeight = refracted ? 0.0 : powerHeuristic(
            brdfDirectionPdf(nee.N, nee.V, direction, nee.albedo, nee.metallic, nee.roughness,
              nee.f0, nee.lobeMode),
            emissiveSourcePdf(emissiveIndex, hit.area, dot(toHit, toHit), cosEmitter));
        }
      }

      vec3 emissiveContribution = throughput * surface.emissive * misWeight;
      recordNonFinite(emissiveContribution, PT_NF_EMISSIVE);
      recordDebugContribution(emissiveContribution, bounce, pathDebug.maxContribution,
        pathDebug.maxBounce);
      radiance += clampContribution(emissiveContribution, settings.fireflyClamp);
      return false;
    }

    return true;
  }

  return false;
}

// --- The bounce loop ---

// The bounce loop, from a vertex whose surface is already known or from a ray. A vertex is P with its
// shading normal N and geometric normal Ng, V pointing back along the ray that reached it. A path from
// a ray - the camera ray past a glass first vertex, or its reflection, see pt_main.rgen - first follows
// rayOrigin along rayDirection to the surface that becomes its bounce zero; nothing before that surface
// gathered next event estimation. Either way the path starts inside the media on the stack, with
// transmissionEvents of the budget spent, and onChain says every vertex before it was a pure delta, see
// PathSettings::chainRefract. Contributions are added to radiance and pathDebug rather than returned,
// so a pixel that runs several paths sums them.
//
// One function for both starts, so every segment of every path is traced from the same call site: a
// ray tracing pipeline schedules invocations by the trace they wait on, and pixels that walk different
// sequences of trace calls measured far slower than the same work from shared ones.
void tracePath(bool fromRay, vec3 rayOrigin, vec3 rayDirection, vec3 P, vec3 N, vec3 Ng, vec3 V,
  vec3 albedo, float metallic, float roughness, vec3 throughput, PathSettings settings, int firstVertex,
  int transmissionEvents, MediumStack media, bool onChain, inout uint rngState, inout vec3 radiance,
  inout PathDebug pathDebug)
{
  g_NonFiniteTracking = settings.trackNonFinite;

  int bounce = fromRay ? -1 : 0;
  vec3 origin = rayOrigin;
  vec3 direction = rayDirection;
  float tMin = 0.0;
  PathNeeVertex nee = PathNeeVertex(false, false, vec3(0.0), vec3(0.0, 1.0, 0.0),
    vec3(0.0, 1.0, 0.0), vec3(0.0), 0.0, 1.0, vec3(0.04), PT_FIRST_VERTEX_COIN);

  // Bounded by the compile-time maximum so the loop is finite whatever maxBounces says; the
  // user's own budget breaks out below.
  for (int step = 0; step <= PT_MAX_BOUNCES + 1; step++)
  {
    g_CurrentBounce = max(bounce, 0);

    if (bounce >= 0)
    {
      float NdotV = clamp(abs(dot(N, V)), 0.01, 0.99);
      vec3 f0 = mix(vec3(0.04), albedo, metallic);

      // Every dimension of the vertex comes off one stream, the first vertex included - see the
      // sampling note above: the BRDF sample and the lobe coin here, then whatever next event
      // estimation draws for its analytic light, the emitter size, the emissive candidates and
      // the resampling coins.
      vec2 brdfXi = randomFloat2(rngState);
      float lobeSelector = randomFloat(rngState);

      // A forced first vertex lobe is sampled with that lobe's density alone, and the emitter MIS
      // weights have to describe the sampler that actually runs.
      int lobeMode = bounce == 0 ? firstVertex : PT_FIRST_VERTEX_COIN;

      // Every ray leaving this vertex meets Solid glass the way the role past it says, the bounce ray
      // the shadow ray's MIS weight is shared with included.
      onChain = onChain && isPureDeltaMirror(roughness, metallic);
      int bounceSolid = solidTreatment(onChain ? settings.chainRefract : settings.secondaryRefract,
        transmissionEvents, settings);

      // The mirror pass gathers no NEE at its first vertex - a delta lobe sees no delta light, and
      // the diffuse half is the base pass's.
      bool neeGathered = firstVertex != PT_FIRST_VERTEX_MIRROR || bounce > 0;
      if (neeGathered)
      {
        vec3 directLight = estimateDirectLight(P, N, Ng, V, albedo, metallic, roughness, f0,
          NdotV, lobeMode, bounceSolid != PT_SOLID_STRAIGHT, settings.glass, rngState);
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
      bool deltaBounce;
      if (firstVertex != PT_FIRST_VERTEX_COIN && bounce == 0)
      {
        // Forced lobe. A selector of 1 is never below the specular probability
        // (clamped to at most 0.9) and 0 always is; multiplying that probability back in
        // cancels the 1 / p the sampler divides by.
        float pSpecular = specularLobeProbability(albedo, f0, metallic, roughness);
        bool basePass = firstVertex == PT_FIRST_VERTEX_BASE;
        if (!sampleBrdfDirection(N, V, albedo, metallic, roughness, f0, NdotV, brdfXi,
          basePass ? 1.0 : 0.0, L, weight, deltaBounce))
          break;
        weight *= basePass ? (1.0 - pSpecular) : pSpecular;
      }
      else if (!sampleBrdfDirection(N, V, albedo, metallic, roughness, f0, NdotV, brdfXi,
        lobeSelector, L, weight, deltaBounce))
        break;

      // Only on the accepting path: a rejected sample leaves both outputs unwritten, and
      // reading them would report a non-finite value the estimator never used.
      recordNonFinite(weight, PT_NF_BRDF_WEIGHT);
      recordNonFinite(L, PT_NF_BRDF_L);

      // A shading normal can tilt a sampled direction below the geometric surface, which no ray
      // leaves through: the path ends there as it does on a rejected sample.
      if (dot(Ng, L) <= 0.0)
        break;

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

      // The ray leaves from the geometric surface and is followed through every dielectric it meets;
      // what ends it - the sky, an emitter - is added on the way.
      nee = PathNeeVertex(neeGathered, deltaBounce, P, N, V, albedo, metallic, roughness, f0, lobeMode);
      origin = offsetRayOrigin(P, Ng);
      direction = L;
      tMin = PT_RAY_TMIN;
    }

    RayHitGeometry hit;
    PathSurface surface;
    vec3 incoming;
    if (!followPathRay(origin, direction, tMin, nee, settings, max(bounce, 0), onChain, throughput,
      transmissionEvents, media, rngState, radiance, pathDebug, hit, surface, incoming))
      break;

    P = hit.position;
    N = surface.normal;
    Ng = hit.normal;
    V = -incoming;
    albedo = surface.albedo;
    metallic = surface.metallic;
    roughness = surface.roughness;
    bounce++;
  }
}

#endif
