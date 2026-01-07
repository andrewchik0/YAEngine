#version 450

layout(location = 0) in vec3 inPos;

layout(push_constant) uniform PushConstants
{
  mat4 vp;
} pc;

layout(location = 0) out vec3 vDir;

void main()
{
  vDir = inPos;
  gl_Position = pc.vp * vec4(inPos, 1.0);
}
