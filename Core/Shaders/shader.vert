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

layout(push_constant) uniform PushConstants
{
  mat4 world;
  mat3 normalMatrix;
  int offset;
} pc;

void main() {
  vec4 worldPos = pc.world * vec4(inPosition, 1.0);
  gl_Position = u_Frame.proj * u_Frame.view * worldPos;
  outCurClipPos = gl_Position;
  outPrevClipPos = u_Frame.prevProj * u_Frame.prevView * worldPos;
  outTexCoord = inTexCoord;
  outPosition = vec3(worldPos);

  vec3 N = normalize(pc.normalMatrix * inNormal);
  vec3 T = normalize(pc.normalMatrix * inTangent.xyz);

  T = normalize(T - N * dot(T, N));
  vec3 B = cross(N, T) * inTangent.w;

  outNormal = N;
  outTBN = mat3(T, B, N);
}
