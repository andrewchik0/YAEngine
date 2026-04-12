layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

#include "common.glsl"
#include "utils.glsl"
#include "octahedron.glsl"
#include "pbr.glsl"

// G-buffer (set 1)
layout(set = 1, binding = 0) uniform sampler2D gbuffer0Texture;
layout(set = 1, binding = 1) uniform sampler2D gbuffer1Texture;
layout(set = 1, binding = 2) uniform sampler2D depthTexture;

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

// IBL (set 3) - cubemap arrays for probe system
layout(set = 3, binding = 0) uniform samplerCubeArray irradianceArray;
layout(set = 3, binding = 1) uniform samplerCubeArray prefilterArray;
layout(set = 3, binding = 2) uniform sampler2D brdfTexture;
layout(set = 3, binding = 3) uniform samplerCube skyboxCubemap;

// Light probes (set 3, binding 4)
#include "../Shared/LightProbeData.h"
layout(std430, set = 3, binding = 4) readonly buffer LightProbeSSBO
{
  LightProbeBuffer u_Probes;
};

const float DEPTH_EPSILON = 1.0;
const int SHADING_PBR = 0;
const int SHADING_UNLIT = 1;
const float MAX_REFLECTION_LOD = 8.0; // log2(256) for probe prefilter

float evaluateProbeWeight(vec3 worldPos, LightProbeInfo probe)
{
  vec3 probePos = probe.positionShape.xyz;
  float shape = probe.positionShape.w;
  vec3 extents = probe.extentsFade.xyz;
  float fade = probe.extentsFade.w;

  if (shape < 0.5) // sphere
  {
    float radius = extents.x;
    float dist = length(worldPos - probePos);
    if (dist > radius) return 0.0;
    float innerRadius = max(0.0, radius - fade);
    if (dist <= innerRadius) return 1.0;
    return 1.0 - (dist - innerRadius) / fade;
  }
  else // box
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
  vec2 brdf = texture(brdfTexture, vec2(NdotV, clamp(roughness, 0.01, 0.99))).rg;
  vec3 F = fresnelSchlickRoughness(NdotV, f0, roughness);
  vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

  return kD * diffuse + specular * (1.0 - clamp(roughness, 0.0, 0.8));
}

void main()
{
  float depth = texture(depthTexture, uv).r;

  // Sky pixels - sample skybox cubemap
  if (depth >= DEPTH_EPSILON)
  {
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 clipPos = vec4(ndc, 1.0, 1.0);
    vec4 viewPos = u_Frame.invProj * clipPos;
    vec3 worldDir = normalize(mat3(u_Frame.invView) * viewPos.xyz);
    vec3 skyColor = texture(skyboxCubemap, worldDir).rgb;
    outColor = vec4(skyColor, 1.0);
    return;
  }

  // Read G-buffer
  vec4 gb0 = texture(gbuffer0Texture, uv);
  vec4 gb1 = texture(gbuffer1Texture, uv);

  vec3 albedo = gb0.rgb;
  float metallic = gb0.a;

  vec3 normal = octDecode(gb1.rg * 2.0 - 1.0);
  float roughness = gb1.b;
  int shadingModel = int(gb1.a * 3.0 + 0.5);

  // Unlit - output albedo directly
  if (shadingModel == SHADING_UNLIT)
  {
    outColor = vec4(albedo, 1.0);
    return;
  }

  // Reconstruct world position from depth
  vec3 viewPos = reconstructViewPos(uv, depth);
  vec3 worldPos = (u_Frame.invView * vec4(viewPos, 1.0)).xyz;

  // PBR IBL lighting with light probe support
  vec3 viewVec = normalize(u_Frame.cameraPosition - worldPos);
  float NdotV = clamp(abs(dot(normal, viewVec)), 0.01, 0.99);
  vec3 f0 = mix(vec3(0.04), albedo, metallic);
  vec3 R = reflect(-viewVec, normal);

  // Probe selection: find best and second-best probe
  vec3 ambient;
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
      {
        ambient = primaryIBL;
      }
      else
      {
        // Blend with secondary probe or skybox fallback (index 0)
        int fallbackIndex = (secondIdx >= 0) ? u_Probes.probes[secondIdx].arrayIndex : 0;
        vec3 fallbackIBL = sampleProbeIBL(normal, R, roughness, NdotV, f0, albedo, metallic, fallbackIndex);
        ambient = mix(fallbackIBL, primaryIBL, bestWeight);
      }
    }
    else
    {
      // No probe covers this pixel - use skybox (index 0)
      ambient = sampleProbeIBL(normal, R, roughness, NdotV, f0, albedo, metallic, 0);
    }
  }
  else
  {
    // No probes at all - use skybox (index 0)
    ambient = sampleProbeIBL(normal, R, roughness, NdotV, f0, albedo, metallic, 0);
  }

  // Analytical lights - tile-culled
  vec3 Lo = vec3(0.0);
  float alpha = roughness * roughness;

  // Directional light (not tile-culled - always applied)
  {
    vec3 L = normalize(-u_Lights.directional.directionIntensity.xyz);
    float intensity = u_Lights.directional.directionIntensity.w;
    vec3 radiance = u_Lights.directional.colorPad.rgb * intensity;

    float shadowFactor = calculateCSMShadow(worldPos, -viewPos.z, normal);
    radiance *= shadowFactor;

    Lo += evaluateDirectLight(normal, viewVec, L, radiance, albedo, metallic, roughness, alpha, f0, NdotV);
  }

  // Look up tile light list
  ivec2 tileCoord = ivec2(gl_FragCoord.xy) / TILE_SIZE;
  int tileIndex = tileCoord.y * u_Frame.tileCountX + tileCoord.x;
  uint tilePtCount = u_Tiles[tileIndex].pointCount;
  uint tileSpCount = u_Tiles[tileIndex].spotCount;

  // Point lights (tile-culled)
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

  // Spot lights (tile-culled)
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

  vec3 resultColor = max(ambient + Lo, vec3(0.0));

  outColor = vec4(resultColor, 1.0);
}
