#version 450

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec4 outColor;

#include "common.glsl"

layout(push_constant) uniform PushConstants
{
  mat4 world;
  vec4 color;
} pc;

void main()
{
  vec4 worldPos = pc.world * vec4(inPosition, 1.0);
  gl_Position = u_Frame.proj * u_Frame.view * worldPos;
  outColor = pc.color;
}
