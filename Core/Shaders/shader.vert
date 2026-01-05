#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec3 outNormal;

layout(set = 0, binding = 0) uniform PerFrameUBO {
  mat4 view;
  mat4 proj;
  float time;
} u_Data;

layout(push_constant) uniform PushConstants
{
  mat4 world;
} pc;

void main() {
  gl_Position = u_Data.proj * u_Data.view * pc.world * vec4(inPosition, 1.0);
  outTexCoord = inTexCoord;
  outNormal = normalize(mat3(pc.world) * inNormal);
}
