#include "../Shared/ShadowData.h"
#include "noise.glsl"

layout(std140, set = 2, binding = 2) uniform ShadowBufferUBO { ShadowBuffer u_Shadow; };
layout(set = 2, binding = 3) uniform sampler2DShadow shadowAtlas;

const float GOLDEN_ANGLE = 2.3999632;
const int SHADOW_SAMPLES = 16;
const float SHADOW_FILTER_RADIUS = 3.0;

vec2 vogelDiskSample(int index, int count, float rotation)
{
  float r = sqrt((float(index) + 0.5) / float(count));
  float theta = float(index) * GOLDEN_ANGLE + rotation;
  return vec2(cos(theta), sin(theta)) * r;
}

int selectCascade(float viewDepth)
{
  for (int i = 0; i < CSM_CASCADE_COUNT; i++)
  {
    if (viewDepth < u_Shadow.cascades[i].splitDepthAndBias.x)
      return i;
  }
  return CSM_CASCADE_COUNT - 1;
}

float sampleShadowPCF(vec3 worldPos, int cascadeIndex)
{
  vec4 viewport = u_Shadow.cascades[cascadeIndex].atlasViewport;

  vec4 clipPos = u_Shadow.cascades[cascadeIndex].viewProj * vec4(worldPos, 1.0);
  vec3 ndc = clipPos.xyz / clipPos.w;

  vec2 uv = ndc.xy * 0.5 + 0.5;

  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
    return 1.0;

  // Map to atlas coordinates with 1-texel border clamp to prevent bleeding
  vec2 texelSize = u_Shadow.atlasSize.zw;
  vec2 atlasUV = viewport.xy + uv * viewport.zw;
  vec2 atlasMin = viewport.xy + texelSize;
  vec2 atlasMax = viewport.xy + viewport.zw - texelSize;

  float rotation = interleavedGradientNoise(gl_FragCoord.xy) * 6.2831853;
  vec2 filterRadius = SHADOW_FILTER_RADIUS * texelSize;

  float shadow = 0.0;
  for (int i = 0; i < SHADOW_SAMPLES; i++)
  {
    vec2 offset = vogelDiskSample(i, SHADOW_SAMPLES, rotation);
    vec2 sampleUV = clamp(atlasUV + offset * filterRadius, atlasMin, atlasMax);
    shadow += texture(shadowAtlas, vec3(sampleUV, ndc.z));
  }
  return shadow / float(SHADOW_SAMPLES);
}

float calculateCSMShadow(vec3 worldPos, float viewDepth, vec3 normal)
{
  if (u_Shadow.shadowsEnabled == 0)
    return 1.0;

  int cascade = selectCascade(viewDepth);

  float normalBias = u_Shadow.cascades[cascade].splitDepthAndBias.z;
  vec3 biasedPos = worldPos + normal * normalBias;

  float shadow = sampleShadowPCF(biasedPos, cascade);

  // Cascade blending
  float splitDist = u_Shadow.cascades[cascade].splitDepthAndBias.x;
  float prevSplit = (cascade > 0) ? u_Shadow.cascades[cascade - 1].splitDepthAndBias.x : 0.0;
  float cascadeRange = splitDist - prevSplit;
  float distToEdge = splitDist - viewDepth;
  float blendZone = cascadeRange * 0.1;

  if (distToEdge < blendZone && cascade < CSM_CASCADE_COUNT - 1)
  {
    float nextShadow = sampleShadowPCF(biasedPos, cascade + 1);
    float blendFactor = distToEdge / blendZone;
    shadow = mix(nextShadow, shadow, blendFactor);
  }

  return shadow;
}

const int SPOT_SHADOW_SAMPLES = 8;
const float SPOT_SHADOW_FILTER_RADIUS = 2.0;
const int POINT_SHADOW_SAMPLES = 8;
const float POINT_SHADOW_FILTER_RADIUS = 2.0;

float sampleShadowAtlasPCF(vec3 worldPos, mat4 lightViewProj, vec4 viewport, int numSamples, float filterRadius, bool clampEdges)
{
  vec4 clipPos = lightViewProj * vec4(worldPos, 1.0);
  vec3 ndc = clipPos.xyz / clipPos.w;

  vec2 uv = ndc.xy * 0.5 + 0.5;

  if (!clampEdges && (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0))
    return 1.0;

  if (ndc.z < 0.0 || ndc.z > 1.0)
    return 1.0;

  uv = clamp(uv, vec2(0.0), vec2(1.0));

  vec2 texelSize = u_Shadow.atlasSize.zw;
  vec2 atlasUV = viewport.xy + uv * viewport.zw;
  vec2 atlasMin = viewport.xy + texelSize;
  vec2 atlasMax = viewport.xy + viewport.zw - texelSize;

  float rotation = interleavedGradientNoise(gl_FragCoord.xy) * 6.2831853;
  vec2 filterRad = filterRadius * texelSize;

  float shadow = 0.0;
  for (int i = 0; i < numSamples; i++)
  {
    vec2 offset = vogelDiskSample(i, numSamples, rotation);
    vec2 sampleUV = clamp(atlasUV + offset * filterRad, atlasMin, atlasMax);
    shadow += texture(shadowAtlas, vec3(sampleUV, ndc.z));
  }
  return shadow / float(numSamples);
}

float calculateSpotShadow(vec3 worldPos, vec3 normal, int spotIndex)
{
  float normalBias = u_Shadow.spotShadows[spotIndex].biasData.y;
  vec3 biasedPos = worldPos + normal * normalBias;

  return sampleShadowAtlasPCF(
    biasedPos,
    u_Shadow.spotShadows[spotIndex].viewProj,
    u_Shadow.spotShadows[spotIndex].atlasViewport,
    SPOT_SHADOW_SAMPLES,
    SPOT_SHADOW_FILTER_RADIUS,
    false);
}

float calculatePointShadow(vec3 worldPos, vec3 normal, vec3 lightPos, int pointIndex)
{
  vec3 dir = worldPos - lightPos;
  vec3 absDir = abs(dir);

  int face;
  if (absDir.x > absDir.y && absDir.x > absDir.z)
    face = dir.x > 0.0 ? 0 : 1;
  else if (absDir.y > absDir.z)
    face = dir.y > 0.0 ? 2 : 3;
  else
    face = dir.z > 0.0 ? 4 : 5;

  float normalBias = u_Shadow.pointShadows[pointIndex].biasData.y;
  vec3 biasedPos = worldPos + normal * normalBias;

  return sampleShadowAtlasPCF(
    biasedPos,
    u_Shadow.pointShadows[pointIndex].faceViewProj[face],
    u_Shadow.pointShadows[pointIndex].faceAtlasViewport[face],
    POINT_SHADOW_SAMPLES,
    POINT_SHADOW_FILTER_RADIUS,
    true);
}
