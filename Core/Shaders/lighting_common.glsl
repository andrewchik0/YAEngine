#ifndef LIGHTING_COMMON_GLSL
#define LIGHTING_COMMON_GLSL

#include "common.glsl"
#include "utils.glsl"
#include "pbr.glsl"
#include "debug_ramps.glsl"

// Lights (set 2)
#include "../Shared/LightData.h"
layout(std430, set = 2, binding = 0) readonly buffer LightBufferSSBO
{
  LightBuffer u_Lights;
};

#include "../Shared/TileCullData.h"
layout(std430, set = 2, binding = 1) readonly buffer TileLightSSBO
{
  TileData u_Tiles[];
};

// Shadows (set 2, bindings 2-3)
#include "shadow.glsl"

// IBL (set 3)
layout(set = 3, binding = 0) uniform samplerCubeArray irradianceArray;
layout(set = 3, binding = 1) uniform samplerCubeArray prefilterArray;
layout(set = 3, binding = 2) uniform sampler2D iblBrdfLut;
layout(set = 3, binding = 3) uniform samplerCube skyboxCubemap;

#include "../Shared/ReflectionProbeData.h"
layout(std430, set = 3, binding = 4) readonly buffer ReflectionProbeSSBO
{
  ReflectionProbeBuffer u_Probes;
};

// Irradiance volumes (set 3, bindings 5-9). One 3D texture per color channel,
// each texel holding (L0, L1x, L1y, L1z) of that channel.
layout(set = 3, binding = 5) uniform sampler3D irradianceVolumeR;
layout(set = 3, binding = 6) uniform sampler3D irradianceVolumeG;
layout(set = 3, binding = 7) uniform sampler3D irradianceVolumeB;
// Kept for documentation and for the validity debug work; the binding itself is
// created by Render.Pipelines.cpp, not by this declaration, deliberately unread in v1 - see computeDiffuseIBL
layout(set = 3, binding = 8) uniform sampler3D irradianceVolumeValidity;

#include "../Shared/IrradianceVolumeData.h"
layout(std140, set = 3, binding = 9) uniform IrradianceVolumeUBO
{
  IrradianceVolumeBuffer u_Volumes;
};

const float MAX_REFLECTION_LOD = 8.0; // log2(256) for probe prefilter

// q is packed as (xyz, w). A zero quaternion falls through as identity, so probes
// that were never given an orientation behave exactly like unrotated ones.
vec3 quatRotate(vec4 q, vec3 v)
{
  return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

vec3 quatRotateInverse(vec4 q, vec3 v)
{
  return quatRotate(vec4(-q.xyz, q.w), v);
}

float evaluateProbeWeight(vec3 worldPos, ReflectionProbeInfo probe)
{
  vec3 probePos = probe.positionShape.xyz;
  float shape = probe.positionShape.w;
  vec3 extents = probe.extentsFade.xyz;
  float fade = probe.extentsFade.w;

  if (shape < 0.5)
  {
    // Rotation-invariant, so the probe orientation is irrelevant here
    float radius = extents.x;
    float dist = length(worldPos - probePos);
    if (dist > radius) return 0.0;
    float innerRadius = max(0.0, radius - fade);
    if (dist <= innerRadius) return 1.0;
    return 1.0 - (dist - innerRadius) / fade;
  }
  else
  {
    vec3 localPos = abs(quatRotateInverse(probe.orientation, worldPos - probePos));
    vec3 outerDist = localPos - extents;
    if (outerDist.x > 0.0 || outerDist.y > 0.0 || outerDist.z > 0.0)
      return 0.0;
    vec3 innerExtents = max(vec3(0.0), extents - vec3(fade));
    vec3 innerDist = localPos - innerExtents;
    vec3 fadeFactors = vec3(1.0);
    for (int i = 0; i < 3; i++)
    {
      if (innerDist[i] > 0.0 && fade > 0.0)
        fadeFactors[i] = 1.0 - innerDist[i] / fade;
    }
    return fadeFactors.x * fadeFactors.y * fadeFactors.z;
  }
}

// Lagarde's parallax-corrected cubemap: walk the reflection ray until it hits the
// probe's proxy volume, then look the prefilter map up along capture point -> hit
// point. Without this every probe reflects as if it were infinitely far away and
// the reflection stays glued to the view direction as the camera moves.
//
// The proxy is a volume of its own, independent of the influence volume: influence
// decides which pixels use the probe, the proxy decides where their rays land. A
// shop window can therefore keep a thin influence slab hugging the glass while
// reprojecting onto a deep box, instead of trading parallax for stray users.
// The lookup direction is measured from the capture point, not from the proxy
// centre, so an offset proxy stays correct.
vec3 parallaxCorrectReflection(vec3 worldPos, vec3 R, ReflectionProbeInfo probe)
{
  if (probe.parallaxCorrection == 0) return R;

  vec3 probePos = probe.positionShape.xyz;
  float shape = probe.positionShape.w;
  vec3 extents = probe.proxyExtents.xyz;
  vec4 q = probe.orientation;
  vec3 proxyOffset = probe.proxyOffset.xyz;

  if (shape < 0.5)
  {
    // Sphere proxy - the shape is rotation invariant, but the offset still rides
    // the probe orientation so the volume tracks the entity when it is rotated
    float radius = extents.x;
    vec3 toCenter = worldPos - (probePos + quatRotate(q, proxyOffset));
    float b = dot(toCenter, R);
    float c = dot(toCenter, toCenter) - radius * radius;
    float disc = b * b - c;
    if (disc <= 0.0) return R;

    float t = -b + sqrt(disc);
    if (t <= 0.0) return R;

    return normalize((worldPos - probePos) + R * t);
  }

  // Box proxy - solved in proxy-local space so a rotated probe corrects against its
  // own axes. Adding proxyOffset back to the hit turns it into a vector from the
  // capture point, which is what the prefilter map was baked around.
  vec3 localPos = quatRotateInverse(q, worldPos - probePos) - proxyOffset;
  vec3 localR = quatRotateInverse(q, R);

  // Rays exactly parallel to a slab would produce 0 * inf, so keep every component finite
  vec3 signR = vec3(localR.x >= 0.0 ? 1.0 : -1.0,
                    localR.y >= 0.0 ? 1.0 : -1.0,
                    localR.z >= 0.0 ? 1.0 : -1.0);
  vec3 invR = signR / max(abs(localR), vec3(1e-5));

  vec3 firstPlane = (-extents - localPos) * invR;
  vec3 secondPlane = (extents - localPos) * invR;
  vec3 furthest = max(firstPlane, secondPlane);
  float t = min(min(furthest.x, furthest.y), furthest.z);
  if (t <= 0.0) return R;

  vec3 localHit = localPos + localR * t;
  return normalize(quatRotate(q, localHit + proxyOffset));
}

vec3 fetchProbePrefilter(vec3 R, float roughness, int arrayIndex)
{
  return textureLod(prefilterArray, vec4(R, float(arrayIndex)), roughness * MAX_REFLECTION_LOD).rgb;
}

// Shading is linear in both maps, so probes can be blended before this runs
// and the BRDF lookup only has to happen once per pixel. The two halves are kept
// as separate functions so the debug views can show one without the other and
// still be exactly the terms shadeIBL adds together.
vec3 shadeDiffuseIBL(vec3 irradiance, float roughness, float NdotV,
  vec3 f0, vec3 albedo, float metallic)
{
  vec3 kD = 1.0 - fresnelSchlickRoughness(NdotV, f0, roughness);
  kD *= (1.0 - metallic);

  return kD * irradiance * albedo;
}

vec3 shadeSpecularIBL(vec3 prefiltered, float roughness, float NdotV, vec3 f0)
{
  vec2 brdf = texture(iblBrdfLut, vec2(NdotV, clamp(roughness, 0.01, 0.99))).rg;
  vec3 F = fresnelSchlickRoughness(NdotV, f0, roughness);
  vec3 specular = prefiltered * (F * brdf.x + brdf.y);

  return specular * (1.0 - clamp(roughness, 0.0, 0.8));
}

vec3 shadeIBL(vec3 irradiance, vec3 prefiltered, float roughness, float NdotV,
  vec3 f0, vec3 albedo, float metallic)
{
  return shadeDiffuseIBL(irradiance, roughness, NdotV, f0, albedo, metallic)
    + shadeSpecularIBL(prefiltered, roughness, NdotV, f0);
}

// Probes actually blended per pixel. Each one costs a prefilter sample, so the
// pool is capped and only the heaviest contributors survive.
#define MAX_BLENDED_REFLECTION_PROBES 3

#define NO_PRIORITY (-999999)

// Diagnostics for the probe debug views, filled by computeSpecularIBL: two scalar
// stores, no extra texture fetches, so the normal path is not affected. They
// describe SPECULAR only now - diffuse no longer goes through probe selection.
// g_ProbeDebugTopIndex is -1 when no local probe influences the pixel.
int g_ProbeDebugTopIndex;
float g_ProbeDebugRemaining;
// Index of the irradiance volume that covered the pixel, -1 when none did and
// the diffuse term came from the skybox. Filled by computeDiffuseIBL.
int g_VolumeDebugIndex;

bool isIndirectDebugView(int view)
{
  return IS_INDIRECT_DEBUG_VIEW(view);
}

// Distinct colors for the first 8 atlas slots, wrapping around beyond that.
// Black is reserved for "no local probe reaches this pixel".
vec3 probeDebugColor(int arrayIndex)
{
  if (arrayIndex < 0) return vec3(0.0);

  int slot = arrayIndex % 8;
  if (slot == 0) return vec3(1.0, 0.15, 0.15);
  if (slot == 1) return vec3(0.15, 1.0, 0.25);
  if (slot == 2) return vec3(0.25, 0.45, 1.0);
  if (slot == 3) return vec3(1.0, 0.95, 0.15);
  if (slot == 4) return vec3(1.0, 0.25, 0.9);
  if (slot == 5) return vec3(0.15, 0.95, 1.0);
  if (slot == 6) return vec3(1.0, 0.55, 0.1);
  return vec3(0.65, 0.25, 1.0);
}

// Heat ramp for the amount that fell through to the skybox - shared ramp,
// see debug_ramps.glsl.
vec3 probeFallbackHeat(float amount)
{
  return debugFallbackHeat(amount);
}

// Skybox irradiance, atlas slot 0. This is the fallback everywhere no volume
// reaches, and the only reason slot 0 of the irradiance cubemap array still exists.
vec3 sampleSkyboxIrradiance(vec3 normal)
{
  return texture(irradianceArray, vec4(normal, 0.0)).rgb;
}

// ALL diffuse indirect sampling lives in this one function on purpose. Upgrading
// to DDGI means replacing its body with a manual 8-tap gather weighted by
// octahedral depth visibility - the asset format, the volume placement and the
// bake stay untouched. Nothing outside may reach into the volume textures.
//
// STORAGE CONVENTION - must match Core/Source/Utils/SphericalHarmonics.h.
// The baked coefficients are ALREADY convolved with the cosine lobe
// (A0 = pi, A1 = 2*pi/3) and divided by pi, so reconstruction is one dot product:
//
//   irradiance(n) = l0 + dot(l1, n)
//
// That is the same quantity irradiance.frag writes (E / pi), which is what
// shadeIBL expects where it multiplies by albedo. Re-applying the cosine
// constants here would double them.
vec3 computeDiffuseIBL(vec3 worldPos, vec3 normal)
{
  g_VolumeDebugIndex = -1;

  int volumeCount = min(u_Volumes.volumeCount, MAX_IRRADIANCE_VOLUMES);
  if (volumeCount == 0)
    return sampleSkyboxIrradiance(normal);

  // v1 leak mitigation: sampling from inside the surface picks up whatever sits
  // on the far side of a wall thinner than the node spacing.
  vec3 samplePos = worldPos + normal * u_Frame.irradianceNormalBias;

  // Walked innermost first - the CPU sorted the volumes by ascending box volume
  // when it filled the UBO. Each one claims a share of what is still unclaimed,
  // so a nested volume fades into whatever encloses it rather than into the sky.
  // Only the outermost boundary, where nothing encloses the point any more, lets
  // the skybox through - and there it is correct, because outside really is sky.
  vec3 accumulated = vec3(0.0);
  float remaining = 1.0;

  for (int i = 0; i < volumeCount; i++)
  {
    vec3 halfExtents = u_Volumes.volumes[i].halfExtentsFade.xyz;
    vec3 localPos = (u_Volumes.volumes[i].worldToLocal * vec4(samplePos, 1.0)).xyz;

    vec3 toFace = halfExtents - abs(localPos);
    if (toFace.x < 0.0 || toFace.y < 0.0 || toFace.z < 0.0)
      continue;

    // Addressing convention lives in Core/Shared/IrradianceVolumeData.h. Nodes sit
    // on a world lattice, so the WORLD sample position goes in as is - the rotated
    // box above only decided whether this volume applies, not where to look it up.
    // The + 0.5 lands on a texel center, which is the half-texel margin that keeps
    // hardware trilinear filtering from reaching into the neighbouring sub-box.
    // The clamp is belt and braces: the lattice covers the whole AABB of the box,
    // so a point that passed the containment test is already inside it.
    vec3 gridSize = u_Volumes.volumes[i].gridSize.xyz;
    vec4 lattice = u_Volumes.volumes[i].latticeOrigin;
    vec3 gridCoord = clamp((samplePos - lattice.xyz) / max(lattice.w, 1e-6),
      vec3(0.0), gridSize - 1.0);
    vec3 atlasTexel = u_Volumes.volumes[i].atlasOrigin.xyz + gridCoord + 0.5;
    vec3 atlasUVW = atlasTexel * u_Volumes.atlasInvSize.xyz;

    // SH coefficients are linear, so three filtered fetches are exact.
    vec4 shR = texture(irradianceVolumeR, atlasUVW);
    vec4 shG = texture(irradianceVolumeG, atlasUVW);
    vec4 shB = texture(irradianceVolumeB, atlasUVW);

    vec3 irradiance = vec3(
      shR.x + dot(shR.yzw, normal),
      shG.x + dot(shG.yzw, normal),
      shB.x + dot(shB.yzw, normal));

    // An L1 fit dips negative on strongly directional environments
    irradiance = max(irradiance, vec3(0.0));

    // Without this the box edge is a hard seam. The fade hands the remainder to
    // whatever encloses this volume, or to the skybox if nothing does.
    // Fade width defaults to half the node spacing.
    float fadeWidth = u_Volumes.volumes[i].halfExtentsFade.w;
    float edgeDistance = min(min(toFace.x, toFace.y), toFace.z);
    float blend = fadeWidth > 1e-6 ? clamp(edgeDistance / fadeWidth, 0.0, 1.0) : 1.0;

    // The coverage view names the innermost volume that actually contributes.
    // A point exactly on a box face has blend == 0 and supplies nothing, so
    // colouring it for that volume would be a lie in the fade band.
    if (g_VolumeDebugIndex < 0 && blend > 0.0)
      g_VolumeDebugIndex = i;

    float share = remaining * blend;
    accumulated += share * irradiance;
    remaining -= share;

    // Deep inside a volume blend is 1 and remaining drops to zero, so the common
    // case still costs a single set of three fetches and exits right here.
    if (remaining < 0.001)
      break;
  }

  // Deferred to here: a pixel deep inside a volume leaves remaining at zero, and
  // the cube fetch would then be multiplied away. The branch is coherent over
  // most of the screen.
  if (remaining < 0.001)
    return accumulated;

  return accumulated + remaining * sampleSkyboxIrradiance(normal);
}

// Specular only. Irradiance left this loop when volumes took over diffuse, which
// halved its per-probe cost: one prefilter fetch instead of a prefilter plus an
// irradiance fetch, up to three times per pixel.
vec3 computeSpecularIBL(vec3 worldPos, vec3 R, float roughness)
{
  int probeCount = min(u_Probes.probeCount, MAX_REFLECTION_PROBES);

  // Single pass over the probes, keeping the strongest few ordered by priority
  // first and weight second
  int selected[MAX_BLENDED_REFLECTION_PROBES];
  float weights[MAX_BLENDED_REFLECTION_PROBES];
  int priorities[MAX_BLENDED_REFLECTION_PROBES];
  for (int k = 0; k < MAX_BLENDED_REFLECTION_PROBES; k++)
  {
    selected[k] = -1;
    weights[k] = 0.0;
    priorities[k] = NO_PRIORITY;
  }

  for (int i = 0; i < probeCount; i++)
  {
    float w = evaluateProbeWeight(worldPos, u_Probes.probes[i]);
    if (w <= 0.0) continue;

    // Insertion sort, pushing the displaced entry further down the list
    int candidateIdx = i;
    float candidateWeight = w;
    int candidatePriority = u_Probes.probes[i].priority;

    for (int k = 0; k < MAX_BLENDED_REFLECTION_PROBES; k++)
    {
      bool outranks = candidatePriority > priorities[k]
        || (candidatePriority == priorities[k] && candidateWeight > weights[k]);
      if (!outranks) continue;

      int swapIdx = selected[k];
      float swapWeight = weights[k];
      int swapPriority = priorities[k];
      selected[k] = candidateIdx;
      weights[k] = candidateWeight;
      priorities[k] = candidatePriority;
      candidateIdx = swapIdx;
      candidateWeight = swapWeight;
      candidatePriority = swapPriority;
    }
  }

  // Top-ranked entry: priority first, then weight - the probe the blend favours
  g_ProbeDebugTopIndex = selected[0] >= 0 ? u_Probes.probes[selected[0]].arrayIndex : -1;

  vec3 blendedPrefiltered = vec3(0.0);
  float remaining = 1.0;

  // Walk the priority levels from the top down, each consuming a share of what is
  // still unclaimed. A nested probe therefore fades into whatever encloses it,
  // however many levels deep, and only the final remainder reaches the skybox.
  int k = 0;
  while (k < MAX_BLENDED_REFLECTION_PROBES && selected[k] >= 0 && remaining > 0.001)
  {
    int levelPriority = priorities[k];
    vec3 levelPrefiltered = vec3(0.0);
    float levelWeight = 0.0;

    while (k < MAX_BLENDED_REFLECTION_PROBES && selected[k] >= 0 && priorities[k] == levelPriority)
    {
      ReflectionProbeInfo probe = u_Probes.probes[selected[k]];

      // The correction is per-probe: each proxy volume bends the ray differently
      vec3 probeR = parallaxCorrectReflection(worldPos, R, probe);

      levelPrefiltered += weights[k] * fetchProbePrefilter(probeR, roughness, probe.arrayIndex);
      levelWeight += weights[k];
      k++;
    }

    // Normalised inside the level so overlaps stay continuous, then scaled by how
    // much of the pixel this level actually covers
    float share = remaining * min(levelWeight, 1.0);
    blendedPrefiltered += share * levelPrefiltered / levelWeight;
    remaining -= share;
  }

  g_ProbeDebugRemaining = remaining;

  if (remaining > 0.001)
  {
    // Skybox fallback has no proxy volume - it is sampled with the raw reflection
    blendedPrefiltered += remaining * fetchProbePrefilter(R, roughness, 0);
  }

  return blendedPrefiltered;
}

// Same work as computeAmbientIBL, with the two halves also handed back on their own
// for the diffuse/specular debug views. They always add up to the returned value,
// so a view can never show a term the normal path does not shade.
//
// This overload additionally takes a diffuse irradiance override from SSGI:
// rgb is the screen-gathered part, alpha the weight of the volume fallback.
// computeDiffuseIBL is then a fallback source, not a replacement - the two terms
// complete each other to a full hemisphere. diffuseNormal is the direction the
// fallback is looked up with (the bent normal when SSGI runs), and this function
// stays the single point that reads the volumes, as promised in
// docs/render-pipeline.md.
vec3 computeAmbientIBLSplit(vec3 worldPos, vec3 normal, vec3 diffuseNormal, vec3 R,
  float roughness, float NdotV, vec3 f0, vec3 albedo, float metallic,
  vec4 ssgiOverride, out vec3 ambientDiffuse, out vec3 ambientSpecular)
{
  // shadeDiffuseIBL scales by (1 - metallic), so on pure metal the three volume
  // fetches plus the skybox fetch inside computeDiffuseIBL are all multiplied away.
  ambientDiffuse = metallic >= 1.0
    ? vec3(0.0)
    : shadeDiffuseIBL(
        ssgiOverride.rgb + ssgiOverride.a * computeDiffuseIBL(worldPos, diffuseNormal),
        roughness, NdotV, f0, albedo, metallic);
  ambientSpecular = shadeSpecularIBL(computeSpecularIBL(worldPos, R, roughness),
    roughness, NdotV, f0);
  return ambientDiffuse + ambientSpecular;
}

// No-override variant: vec4(0, 0, 0, 1) is the identity - no screen part, the
// volume fallback carries the whole hemisphere.
vec3 computeAmbientIBLSplit(vec3 worldPos, vec3 normal, vec3 R, float roughness, float NdotV,
  vec3 f0, vec3 albedo, float metallic, out vec3 ambientDiffuse, out vec3 ambientSpecular)
{
  return computeAmbientIBLSplit(worldPos, normal, normal, R, roughness, NdotV,
    f0, albedo, metallic, vec4(0.0, 0.0, 0.0, 1.0), ambientDiffuse, ambientSpecular);
}

// Kept so both fragment shaders stay one call away from the full ambient term and
// cannot drift apart on which halves they combine.
vec3 computeAmbientIBL(vec3 worldPos, vec3 normal, vec3 R, float roughness, float NdotV,
  vec3 f0, vec3 albedo, float metallic)
{
  vec3 ambientDiffuse;
  vec3 ambientSpecular;
  return computeAmbientIBLSplit(worldPos, normal, R, roughness, NdotV,
    f0, albedo, metallic, ambientDiffuse, ambientSpecular);
}

vec3 computeDirectLighting(vec3 worldPos, vec3 viewPos, vec3 normal, vec3 viewVec,
  vec3 albedo, float metallic, float roughness, vec3 f0, float NdotV,
  ivec2 fragCoord)
{
  vec3 Lo = vec3(0.0);
  float alpha = roughness * roughness;

  // Directional light (not tile-culled)
  {
    vec3 L = normalize(-u_Lights.directional.directionIntensity.xyz);
    float intensity = u_Lights.directional.directionIntensity.w;
    vec3 radiance = u_Lights.directional.colorPad.rgb * intensity;

    float shadowFactor = calculateCSMShadow(worldPos, -viewPos.z, normal);
    radiance *= shadowFactor;

    Lo += evaluateDirectLight(normal, viewVec, L, radiance, albedo, metallic, roughness, alpha, f0, NdotV);
  }

  // Tile light lookup
  ivec2 tileCoord = fragCoord / TILE_SIZE;
  int tileIndex = tileCoord.y * u_Frame.tileCountX + tileCoord.x;
  uint tilePtCount = u_Tiles[tileIndex].pointCount;
  uint tileSpCount = u_Tiles[tileIndex].spotCount;

  for (uint t = 0; t < tilePtCount; t++)
  {
    int i = int(u_Tiles[tileIndex].indices[t]);
    vec3 lightPos = u_Lights.pointLights[i].positionRadius.xyz;
    float lightRadius = u_Lights.pointLights[i].positionRadius.w;

    vec3 L = lightPos - worldPos;
    float dist = length(L);
    if (dist > lightRadius) continue;
    L /= dist;

    float att = 1.0 - (dist * dist) / (lightRadius * lightRadius);
    att = att * att;
    vec3 radiance = u_Lights.pointLights[i].colorIntensity.rgb * u_Lights.pointLights[i].colorIntensity.w * att;

    int pointShadowIdx = floatBitsToInt(u_Lights.pointLights[i].shadowPad.x);
    if (pointShadowIdx >= 0)
      radiance *= calculatePointShadow(worldPos, normal, lightPos, pointShadowIdx);

    Lo += evaluateDirectLight(normal, viewVec, L, radiance, albedo, metallic, roughness, alpha, f0, NdotV);
  }

  for (uint t = 0; t < tileSpCount; t++)
  {
    int i = int(u_Tiles[tileIndex].indices[tilePtCount + t]);
    vec3 lightPos = u_Lights.spotLights[i].positionRadius.xyz;
    float lightRadius = u_Lights.spotLights[i].positionRadius.w;

    vec3 L = lightPos - worldPos;
    float dist = length(L);
    if (dist > lightRadius) continue;
    L /= dist;

    vec3 lightDir = u_Lights.spotLights[i].directionInnerCone.xyz;
    float innerCos = u_Lights.spotLights[i].directionInnerCone.w;
    float outerCos = u_Lights.spotLights[i].colorOuterCone.w;
    float theta = dot(L, -lightDir);
    float spotFactor = clamp((theta - outerCos) / (innerCos - outerCos), 0.0, 1.0);

    float att = 1.0 - (dist * dist) / (lightRadius * lightRadius);
    att = att * att;
    vec3 radiance = u_Lights.spotLights[i].colorOuterCone.rgb * u_Lights.spotLights[i].intensityShadow.x * att * spotFactor;

    int spotShadowIdx = floatBitsToInt(u_Lights.spotLights[i].intensityShadow.y);
    if (spotShadowIdx >= 0)
      radiance *= calculateSpotShadow(worldPos, normal, lightPos, spotShadowIdx);

    Lo += evaluateDirectLight(normal, viewVec, L, radiance, albedo, metallic, roughness, alpha, f0, NdotV);
  }

  return Lo;
}

float computeHeightFog(vec3 rayOrigin, vec3 rayDir, float rayLength)
{
  float effectiveLength = max(rayLength - u_Frame.fogStartDistance, 0.0);
  if (effectiveLength <= 0.0) return 0.0;

  float b = u_Frame.fogHeightFalloff;

  float startY = rayOrigin.y + rayDir.y * u_Frame.fogStartDistance;

  float dirY = rayDir.y;
  float safeDirY = dirY >= 0.0 ? max(dirY, 0.001) : min(dirY, -0.001);

  float bStartY = clamp(-b * startY, -20.0, 20.0);
  float bDirLen = clamp(-effectiveLength * safeDirY * b, -20.0, 20.0);
  float fogInt = u_Frame.fogDensity * exp(bStartY)
    * (1.0 - exp(bDirLen)) / (safeDirY * b);

  return clamp(fogInt, 0.0, u_Frame.fogMaxOpacity);
}

#endif
