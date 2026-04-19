#version 450

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outColor;

#include "common.glsl"
#include "../Shared/ParticleInstance.h"

layout(std430, set = 1, binding = 0) readonly buffer ParticleBuffer
{
  ParticleInstance u_Particles[];
};

invariant gl_Position;

void main()
{
  ParticleInstance p = u_Particles[gl_InstanceIndex];
  vec3 head = p.headAndWidth.xyz;
  float width = p.headAndWidth.w;
  vec3 tail = p.tail.xyz;

  vec3 axis = head - tail;
  float axisLen = length(axis);
  const float kEps = 1e-5;

  vec3 perp = vec3(0.0);
  if (axisLen > kEps)
  {
    vec3 axisDir = axis / axisLen;
    vec3 midpoint = (head + tail) * 0.5;
    vec3 viewDir = normalize(u_Frame.cameraPosition - midpoint);
    vec3 perpRaw = cross(viewDir, axisDir);
    float perpLen = length(perpRaw);
    if (perpLen > kEps)
      perp = (perpRaw / perpLen) * (width * 0.5);
  }

  // 4-vertex triangle strip. Bits of gl_VertexIndex:
  //   bit 1 -> head (1) / tail (0)
  //   bit 0 -> +perp (1) / -perp (0)
  bool isHead = (gl_VertexIndex & 2) != 0;
  bool isPositive = (gl_VertexIndex & 1) != 0;
  vec3 anchor = isHead ? head : tail;
  vec3 worldPos = anchor + (isPositive ? perp : -perp);
  vec2 uv = vec2(isHead ? 1.0 : 0.0, isPositive ? 1.0 : 0.0);

  if (axisLen <= kEps)
  {
    // Degenerate streak (zero-length velocity) - cull behind near plane.
    gl_Position = vec4(0.0, 0.0, -2.0, 1.0);
  }
  else
  {
    gl_Position = u_Frame.proj * u_Frame.view * vec4(worldPos, 1.0);
    gl_Position.xy += vec2(u_Frame.jitterX, u_Frame.jitterY) * gl_Position.w;
  }

  outUV = uv;
  outColor = p.color;
}
