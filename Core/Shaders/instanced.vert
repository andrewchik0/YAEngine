#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outPosition;
layout(location = 3) out mat3 outTBN;

layout(set = 0, binding = 0) uniform PerFrameUBO {
  mat4 view;
  mat4 proj;
  vec3 cameraPosition;
  float time;
  vec3 cameraDirection;
} u_Data;

layout(set = 2, binding = 0) readonly buffer Instances
{
  mat4 data[];
} instances;

layout(push_constant) uniform PushConstants
{
  mat4 world;
} pc;

void main() {
  gl_Position = u_Data.proj * u_Data.view * pc.world * instances.data[gl_InstanceIndex] * vec4(inPosition, 1.0);
  outTexCoord = inTexCoord;
  outPosition = vec3(pc.world * instances.data[gl_InstanceIndex] * vec4(inPosition, 1.0));
  outNormal = normalize(mat3(pc.world) * inNormal);

  mat3 normalMatrix = transpose(inverse(mat3(pc.world)));

  vec3 N = normalize(normalMatrix * inNormal);
  vec3 T = normalize(normalMatrix * inTangent.xyz);

  T = normalize(T - N * dot(T, N));
  vec3 B = cross(N, T) * inTangent.w;

  outTBN = mat3(T, B, N);
}
