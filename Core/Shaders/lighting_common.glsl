#ifndef LIGHTING_COMMON_GLSL
#define LIGHTING_COMMON_GLSL

#include "common.glsl"
#include "utils.glsl"
#include "pbr.glsl"

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

#include "../Shared/LightProbeData.h"
layout(std430, set = 3, binding = 4) readonly buffer LightProbeSSBO
{
  LightProbeBuffer u_Probes;
};

const float MAX_REFLECTION_LOD = 8.0; // log2(256) for probe prefilter

float evaluateProbeWeight(vec3 worldPos, LightProbeInfo probe)
{
  vec3 probePos = probe.positionShape.xyz;
  float shape = probe.positionShape.w;
  vec3 extents = probe.extentsFade.xyz;
  float fade = probe.extentsFade.w;

  if (shape < 0.5)
  {
    float radius = extents.x;
    float dist = length(worldPos - probePos);
    if (dist > radius) return 0.0;
    float innerRadius = max(0.0, radius - fade);
    if (dist <= innerRadius) return 1.0;
    return 1.0 - (dist - innerRadius) / fade;
  }
  else
  {
    vec3 localPos = abs(worldPos - probePos);
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

void fetchProbeMaps(vec3 normal, vec3 R, float roughness, int arrayIndex,
  out vec3 irradiance, out vec3 prefiltered)
{
  irradiance = texture(irradianceArray, vec4(normal, float(arrayIndex))).rgb;
  prefiltered = textureLod(prefilterArray, vec4(R, float(arrayIndex)), roughness * MAX_REFLECTION_LOD).rgb;
}

// Shading is linear in both maps, so probes can be blended before this runs
// and the BRDF lookup only has to happen once per pixel.
vec3 shadeIBL(vec3 irradiance, vec3 prefiltered, float roughness, float NdotV,
  vec3 f0, vec3 albedo, float metallic)
{
  vec3 kD = 1.0 - fresnelSchlickRoughness(NdotV, f0, roughness);
  kD *= (1.0 - metallic);

  vec3 diffuse = irradiance * albedo;

  vec2 brdf = texture(iblBrdfLut, vec2(NdotV, clamp(roughness, 0.01, 0.99))).rg;
  vec3 F = fresnelSchlickRoughness(NdotV, f0, roughness);
  vec3 specular = prefiltered * (F * brdf.x + brdf.y);

  return kD * diffuse + specular * (1.0 - clamp(roughness, 0.0, 0.8));
}

vec3 sampleProbeIBL(vec3 normal, vec3 R, float roughness, float NdotV,
  vec3 f0, vec3 albedo, float metallic, int arrayIndex)
{
  vec3 irradiance, prefiltered;
  fetchProbeMaps(normal, R, roughness, arrayIndex, irradiance, prefiltered);
  return shadeIBL(irradiance, prefiltered, roughness, NdotV, f0, albedo, metallic);
}

// Probes actually blended per pixel. Each one costs an irradiance plus a prefilter
// sample, so the pool is capped and only the heaviest contributors survive.
#define MAX_BLENDED_PROBES 3

#define NO_PRIORITY (-999999)

vec3 computeAmbientIBL(vec3 worldPos, vec3 normal, vec3 R, float roughness, float NdotV,
  vec3 f0, vec3 albedo, float metallic)
{
  int probeCount = min(u_Probes.probeCount, MAX_LIGHT_PROBES);

  // Single pass over the probes, keeping the strongest few ordered by priority
  // first and weight second
  int selected[MAX_BLENDED_PROBES];
  float weights[MAX_BLENDED_PROBES];
  int priorities[MAX_BLENDED_PROBES];
  for (int k = 0; k < MAX_BLENDED_PROBES; k++)
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

    for (int k = 0; k < MAX_BLENDED_PROBES; k++)
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

  vec3 blendedIrradiance = vec3(0.0);
  vec3 blendedPrefiltered = vec3(0.0);
  float remaining = 1.0;

  // Walk the priority levels from the top down, each consuming a share of what is
  // still unclaimed. A nested probe therefore fades into whatever encloses it,
  // however many levels deep, and only the final remainder reaches the skybox.
  int k = 0;
  while (k < MAX_BLENDED_PROBES && selected[k] >= 0 && remaining > 0.001)
  {
    int levelPriority = priorities[k];
    vec3 levelIrradiance = vec3(0.0);
    vec3 levelPrefiltered = vec3(0.0);
    float levelWeight = 0.0;

    while (k < MAX_BLENDED_PROBES && selected[k] >= 0 && priorities[k] == levelPriority)
    {
      vec3 irradiance, prefiltered;
      fetchProbeMaps(normal, R, roughness, u_Probes.probes[selected[k]].arrayIndex,
        irradiance, prefiltered);

      levelIrradiance += weights[k] * irradiance;
      levelPrefiltered += weights[k] * prefiltered;
      levelWeight += weights[k];
      k++;
    }

    // Normalised inside the level so overlaps stay continuous, then scaled by how
    // much of the pixel this level actually covers
    float share = remaining * min(levelWeight, 1.0);
    blendedIrradiance += share * levelIrradiance / levelWeight;
    blendedPrefiltered += share * levelPrefiltered / levelWeight;
    remaining -= share;
  }

  if (remaining > 0.001)
  {
    vec3 skyIrradiance, skyPrefiltered;
    fetchProbeMaps(normal, R, roughness, 0, skyIrradiance, skyPrefiltered);
    blendedIrradiance += remaining * skyIrradiance;
    blendedPrefiltered += remaining * skyPrefiltered;
  }

  return shadeIBL(blendedIrradiance, blendedPrefiltered, roughness, NdotV, f0, albedo, metallic);
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
      radiance *= calculateSpotShadow(worldPos, normal, spotShadowIdx);

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
