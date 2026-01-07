#version 450

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform PushConstants
{
  mat4 camDir;
  mat4 proj;
} pc;

layout(location = 0) out vec3 outCoord;

void main()
{
  outCoord = mat3(pc.camDir) * inPosition;

  vec4 pos = pc.proj * vec4(inPosition, 1.0);
  gl_Position = pos.xyww;
}
