#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inTangent;

invariant gl_Position;

#include "common.glsl"

layout(set = 1, binding = 0) readonly buffer Instances
{
  mat4 data[];
} instances;

layout(push_constant) uniform PushConstants
{
  mat4 world;
  int offset;
} pc;

void main() {
  mat4 worldMatrix = pc.world * instances.data[gl_InstanceIndex + pc.offset];
  vec4 worldPos = worldMatrix * vec4(inPosition, 1.0);
  gl_Position = u_Frame.proj * u_Frame.view * worldPos;
}
