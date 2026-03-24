#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

#include "common.glsl"
#include "utils.glsl"

layout(set = 1, binding = 0) uniform sampler2D ssaoTexture;
layout(set = 1, binding = 1) uniform sampler2D depthTexture;

void main()
{
  vec2 texelSize = 1.0 / vec2(float(u_Data.screenWidth), float(u_Data.screenHeight));
  float centerDepth = linearizeDepth(textureLod(depthTexture, uv, 0.0).r);

  float result = 0.0;
  float totalWeight = 0.0;

  // 5x5 bilateral blur
  for (int x = -2; x <= 2; x++)
  {
    for (int y = -2; y <= 2; y++)
    {
      vec2 offset = vec2(float(x), float(y)) * texelSize;
      vec2 sampleUV = uv + offset;

      float ao = textureLod(ssaoTexture, sampleUV, 0.0).r;
      float sampleDepth = linearizeDepth(textureLod(depthTexture, sampleUV, 0.0).r);

      // Spatial weight: gaussian
      float dist2 = float(x * x + y * y);
      float spatialWeight = exp(-dist2 / 8.0);

      // Depth weight: reject samples across depth discontinuities
      float depthDiff = abs(centerDepth - sampleDepth) / max(centerDepth, 0.001);
      float depthWeight = exp(-depthDiff * depthDiff * 2000.0);

      float weight = spatialWeight * depthWeight;
      result += ao * weight;
      totalWeight += weight;
    }
  }

  result /= totalWeight;
  outColor = vec4(result, result, result, 1.0);
}
