#version 450

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec4 outColor;

#include "common.glsl"
#include "../Shared/GizmoPushConstants.h"

layout(push_constant) uniform PushConstantBlock { GizmoPushConstants pc; };

void main()
{
  vec4 worldPos = pc.world * vec4(inPosition, 1.0);
  // Gizmos draw after the temporal resolve over an already stabilized image,
  // so the camera jitter that u_Frame.proj carries must not reach them.
  gl_Position = u_Frame.unjitteredProj * u_Frame.view * worldPos;
  outColor = pc.color;
}
