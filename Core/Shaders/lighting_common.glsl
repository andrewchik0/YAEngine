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

vec3 sampleProbeIBL(vec3 normal, vec3 R, float roughness, float NdotV,
  vec3 f0, vec3 albedo, float metallic, int arrayIndex)
{
  vec3 kD = 1.0 - fresnelSchlickRoughness(NdotV, f0, roughness);
  kD *= (1.0 - metallic);

  vec3 irradiance = texture(irradianceArray, vec4(normal, float(arrayIndex))).rgb;
  vec3 diffuse = irradiance * albedo;

  vec3 prefilteredColor = textureLod(prefilterArray, vec4(R, float(arrayIndex)), roughness * MAX_REFLECTION_LOD).rgb;
  vec2 brdf = texture(iblBrdfLut, vec2(NdotV, clamp(roughness, 0.01, 0.99))).rg;
  vec3 F = fresnelSchlickRoughness(NdotV, f0, roughness);
  vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

  return kD * diffuse + specular * (1.0 - clamp(roughness, 0.0, 0.8));
}

vec3 computeAmbientIBL(vec3 worldPos, vec3 normal, vec3 R, float roughness, float NdotV,
  vec3 f0, vec3 albedo, float metallic)
{
  if (u_Probes.probeCount > 0)
  {
    int bestIdx = -1;
    float bestWeight = 0.0;
    int bestPriority = -999999;
    int secondIdx = -1;
    float secondWeight = 0.0;
    int secondPriority = -999999;

    for (int i = 0; i < min(u_Probes.probeCount, MAX_LIGHT_PROBES); i++)
    {
      float w = evaluateProbeWeight(worldPos, u_Probes.probes[i]);
      if (w <= 0.0) continue;

      int pri = u_Probes.probes[i].priority;
      if (pri > bestPriority || (pri == bestPriority && w > bestWeight))
      {
        secondIdx = bestIdx;
        secondWeight = bestWeight;
        secondPriority = bestPriority;
        bestIdx = i;
        bestWeight = w;
        bestPriority = pri;
      }
      else if (pri > secondPriority || (pri == secondPriority && w > secondWeight))
      {
        secondIdx = i;
        secondWeight = w;
        secondPriority = pri;
      }
    }

    if (bestIdx >= 0)
    {
      vec3 primaryIBL = sampleProbeIBL(normal, R, roughness, NdotV, f0, albedo, metallic,
        u_Probes.probes[bestIdx].arrayIndex);

      if (bestWeight >= 1.0)
        return primaryIBL;

      int fallbackIndex = (secondIdx >= 0) ? u_Probes.probes[secondIdx].arrayIndex : 0;
      vec3 fallbackIBL = sampleProbeIBL(normal, R, roughness, NdotV, f0, albedo, metallic, fallbackIndex);
      return mix(fallbackIBL, primaryIBL, bestWeight);
    }
    return sampleProbeIBL(normal, R, roughness, NdotV, f0, albedo, metallic, 0);
  }
  return sampleProbeIBL(normal, R, roughness, NdotV, f0, albedo, metallic, 0);
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
