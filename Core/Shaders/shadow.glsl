#include "../Shared/ShadowData.h"

layout(std140, set = 2, binding = 2) uniform ShadowBufferUBO { ShadowBuffer u_Shadow; };
layout(set = 2, binding = 3) uniform sampler2D shadowAtlas;

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
  vec4 cascade = u_Shadow.cascades[cascadeIndex].splitDepthAndBias;
  vec4 viewport = u_Shadow.cascades[cascadeIndex].atlasViewport;
  float bias = cascade.y;

  vec4 clipPos = u_Shadow.cascades[cascadeIndex].viewProj * vec4(worldPos, 1.0);
  vec3 ndc = clipPos.xyz / clipPos.w;

  // NDC to UV [0,1]
  vec2 uv = ndc.xy * 0.5 + 0.5;

  // Check if in shadow map bounds
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
    return 1.0;

  // Map to atlas coordinates
  vec2 atlasUV = viewport.xy + uv * viewport.zw;
  float compareDepth = ndc.z - bias;

  // PCF 3x3
  vec2 texelSize = u_Shadow.atlasSize.zw;
  float shadow = 0.0;
  for (int x = -1; x <= 1; x++)
  {
    for (int y = -1; y <= 1; y++)
    {
      float depth = texture(shadowAtlas, atlasUV + vec2(x, y) * texelSize).r;
      shadow += (compareDepth > depth) ? 0.0 : 1.0;
    }
  }
  return shadow / 9.0;
}

float calculateCSMShadow(vec3 worldPos, float viewDepth, vec3 normal)
{
  if (u_Shadow.shadowsEnabled == 0)
    return 1.0;

  int cascade = selectCascade(viewDepth);

  // Normal bias: offset along normal to reduce shadow acne on slopes
  float normalBias = u_Shadow.cascades[cascade].splitDepthAndBias.z;
  vec3 biasedPos = worldPos + normal * normalBias;

  float shadow = sampleShadowPCF(biasedPos, cascade);

  // Cascade blending: smooth transition at cascade boundaries
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
