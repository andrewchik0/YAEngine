#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

#include "common.glsl"
#include "utils.glsl"

layout(set = 1, binding = 0) uniform sampler2D depthTexture;
layout(set = 1, binding = 1) uniform sampler2D normalTexture;
layout(set = 1, binding = 2) uniform sampler2D noiseTexture;

#include "../Shared/SSAOKernel.h"
layout(set = 1, binding = 3) uniform SSAOKernelBlock { SSAOKernel u_Kernel; };

const float RADIUS = 0.2;
const float BIAS = 0.025;
const float INTENSITY = 1.5;

void main()
{
  float depth = textureLod(depthTexture, uv, 0.0).r;

  if (depth >= 0.9999)
  {
    outColor = vec4(1.0);
    return;
  }

  vec3 viewPos = reconstructViewPos(uv, depth);
  float linearDepthCenter = linearizeDepth(depth);
  vec3 worldNormal = textureLod(normalTexture, uv, 0.0).rgb;

  if (dot(worldNormal, worldNormal) < 0.000001)
  {
    outColor = vec4(1.0);
    return;
  }

  worldNormal = normalize(worldNormal);
  vec3 viewNormal = normalize(mat3(u_Frame.view) * worldNormal);

  // Full-res noise tiling: noise is 4x4, screen is full-res
  vec2 noiseScale = vec2(float(u_Frame.screenWidth) / 4.0, float(u_Frame.screenHeight) / 4.0);
  vec3 randomVec = textureLod(noiseTexture, uv * noiseScale, 0.0).rgb;

  vec3 tangent = normalize(randomVec - viewNormal * dot(randomVec, viewNormal));
  vec3 bitangent = cross(viewNormal, tangent);
  mat3 TBN = mat3(tangent, bitangent, viewNormal);

  float occlusion = 0.0;

  for (int i = 0; i < SSAO_KERNEL_SIZE; i++)
  {
    vec3 samplePos = viewPos + (TBN * u_Kernel.samples[i].xyz) * RADIUS;

    // Project to clip space
    vec4 clip = u_Frame.proj * vec4(samplePos, 1.0);
    vec2 sampleUV = (clip.xy / clip.w) * 0.5 + 0.5;

    float sampledDepth = textureLod(depthTexture, sampleUV, 0.0).r;

    // Mask out skybox samples
    float notSky = step(sampledDepth, 0.9998);

    float sampleLinearDepth = linearizeDepth(sampledDepth);
    float expectedLinearDepth = linearizeDepth(clip.z / clip.w);

    // Occlusion: sampled surface is closer than expected sample position
    // Scale bias with distance to prevent Z-fighting at far range
    float scaledBias = BIAS * max(1.0, linearDepthCenter * 0.1);
    float depthDiff = sampleLinearDepth - expectedLinearDepth;
    float isOccluded = step(scaledBias, -depthDiff);

    // Range check: smooth, depth-proportional rejection of distant occluders
    // When sample is farther than center, rangeCheck = 1.0
    float maxRange = max(RADIUS * 2.0, linearDepthCenter * 0.02);
    float rangeSmooth = smoothstep(maxRange, maxRange * 0.5, abs(linearDepthCenter - sampleLinearDepth));
    float rangeCheck = mix(1.0, rangeSmooth, step(sampleLinearDepth, linearDepthCenter));

    occlusion += isOccluded * rangeCheck * notSky;
  }

  float ao = 1.0 - (occlusion / float(SSAO_KERNEL_SIZE)) * INTENSITY;
  outColor = vec4(clamp(ao, 0.0, 1.0));
}
