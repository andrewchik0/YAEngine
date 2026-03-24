#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inTangent;

invariant gl_Position;

#include "common.glsl"

layout(push_constant) uniform PushConstants
{
  mat4 world;
  int offset;
} pc;

void main() {
  vec4 worldPos = pc.world * vec4(inPosition, 1.0);
  gl_Position = u_Data.proj * u_Data.view * worldPos;
}
