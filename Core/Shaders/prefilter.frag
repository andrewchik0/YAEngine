#version 450

layout(set = 0, binding = 0) uniform samplerCube uCubemap;

layout(location = 0) in vec3 vDir;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants
{
  mat4 vp;
  float roughness;
} pc;

#include "pbr.glsl"

void main()
{
  vec3 N = normalize(vDir);
  vec3 R = N;
  vec3 V = R;

  const uint SAMPLE_COUNT = 2048u;
  float totalWeight = 0.0;
  vec3 prefilteredColor = vec3(0.0);

  for (uint i = 0u; i < SAMPLE_COUNT; ++i)
  {
    vec2 Xi = hammersley(i, SAMPLE_COUNT);
    vec3 H = importanceSampleGGX(Xi, N, pc.roughness);
    vec3 L = normalize(2.0 * dot(V, H) * H - V);

    float NdotL = max(dot(N, L), 0.0);
    if (NdotL > 0.0)
    {
      prefilteredColor += texture(uCubemap, vec3(L.x, -L.y, L.z)).rgb * NdotL;
      totalWeight += NdotL;
    }
  }

  prefilteredColor /= max(totalWeight, 0.001);
  outColor = vec4(prefilteredColor, 1.0);
}
