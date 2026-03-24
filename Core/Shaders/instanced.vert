#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outPosition;
layout(location = 3) out mat3 outTBN;
layout(location = 6) out vec4 outCurClipPos;
layout(location = 7) out vec4 outPrevClipPos;

invariant gl_Position;

#include "common.glsl"

layout(set = 2, binding = 0) readonly buffer Instances
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
  gl_Position = u_Data.proj * u_Data.view * worldPos;
  outCurClipPos = gl_Position;
  outPrevClipPos = u_Data.prevProj * u_Data.prevView * worldPos;
  outTexCoord = inTexCoord;
  outPosition = vec3(worldPos);

  // Normal matrix computed on GPU — per-instance world matrix is only known here
  mat3 normalMatrix = transpose(inverse(mat3(worldMatrix)));

  vec3 N = normalize(normalMatrix * inNormal);
  vec3 T = normalize(normalMatrix * inTangent.xyz);

  T = normalize(T - N * dot(T, N));
  vec3 B = cross(N, T) * inTangent.w;

  outNormal = N;
  outTBN = mat3(T, B, N);
}
