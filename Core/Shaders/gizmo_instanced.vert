#version 450

layout(location = 0) in vec3 inPosition;

// Per-instance: a world matrix as four columns plus the color. Same payload the
// push-constant path carries, moved to a vertex buffer so one draw covers the
// whole batch - the volume node overlay alone can request tens of thousands.
layout(location = 1) in vec4 inWorld0;
layout(location = 2) in vec4 inWorld1;
layout(location = 3) in vec4 inWorld2;
layout(location = 4) in vec4 inWorld3;
layout(location = 5) in vec4 inColor;

layout(location = 0) out vec4 outColor;

#include "common.glsl"

void main()
{
  mat4 world = mat4(inWorld0, inWorld1, inWorld2, inWorld3);
  vec4 worldPos = world * vec4(inPosition, 1.0);
  // Gizmos draw after the temporal resolve over an already stabilized image,
  // so the camera jitter that u_Frame.proj carries must not reach them.
  gl_Position = u_Frame.unjitteredProj * u_Frame.view * worldPos;
  outColor = inColor;
}
