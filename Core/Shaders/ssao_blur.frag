layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

#include "common.glsl"
#include "utils.glsl"

layout(set = 1, binding = 0) uniform sampler2D ssaoTexture;
layout(set = 1, binding = 1) uniform sampler2D depthTexture;

layout(push_constant) uniform PushConstants
{
  int direction; // 0 = horizontal, 1 = vertical
} pc;

void main()
{
  vec2 texelSize = 1.0 / vec2(float(u_Frame.screenWidth), float(u_Frame.screenHeight));
  float centerDepth = linearizeDepth(textureLod(depthTexture, uv, 0.0).r);

  float result = 0.0;
  float totalWeight = 0.0;

  // Separable bilateral blur: 5 samples along one axis
  vec2 step = (pc.direction == 0)
    ? vec2(texelSize.x, 0.0)
    : vec2(0.0, texelSize.y);

  for (int i = -2; i <= 2; i++)
  {
    vec2 sampleUV = uv + step * float(i);

    float ao = textureLod(ssaoTexture, sampleUV, 0.0).r;
    float sampleDepth = linearizeDepth(textureLod(depthTexture, sampleUV, 0.0).r);

    // Spatial weight: gaussian
    float dist2 = float(i * i);
    float spatialWeight = exp(-dist2 / 4.5);

    // Depth weight: reject samples across depth discontinuities
    float depthDiff = abs(centerDepth - sampleDepth) / max(centerDepth, 0.001);
    float depthWeight = exp(-depthDiff * depthDiff * 2000.0);

    float weight = spatialWeight * depthWeight;
    result += ao * weight;
    totalWeight += weight;
  }

  result /= totalWeight;
  outColor = vec4(result, result, result, 1.0);
}
