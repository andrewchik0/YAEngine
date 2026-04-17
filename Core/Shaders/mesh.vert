layout(location = 0) in vec3 inPosition;

#if !defined(DEPTH_ONLY) || defined(ALPHA_TEST)
layout(location = 1) in vec2 inTexCoord;
layout(location = 0) out vec2 outTexCoord;
#endif

#ifndef DEPTH_ONLY
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inTangent;

layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outPosition;
layout(location = 3) out mat3 outTBN;
layout(location = 6) out vec4 outCurClipPos;
layout(location = 7) out vec4 outPrevClipPos;
#endif

invariant gl_Position;

#include "common.glsl"

#ifdef INSTANCED
  #if defined(DEPTH_ONLY) && !defined(ALPHA_TEST)
layout(set = 1, binding = 0) readonly buffer Instances
  #else
layout(set = 2, binding = 0) readonly buffer Instances
  #endif
{
  mat4 data[];
} instances;
#endif

layout(push_constant) uniform PushConstants
{
  mat4 world;
  int offset;
} pc;

void main() {
#ifdef INSTANCED
  mat4 worldMatrix = pc.world * instances.data[gl_InstanceIndex + pc.offset];
#else
  mat4 worldMatrix = pc.world;
#endif

  vec4 worldPos = worldMatrix * vec4(inPosition, 1.0);
  gl_Position = u_Frame.proj * u_Frame.view * worldPos;

#if !defined(DEPTH_ONLY) || defined(ALPHA_TEST)
  outTexCoord = inTexCoord;
#endif

#ifndef DEPTH_ONLY
  outCurClipPos = gl_Position;
  outPrevClipPos = u_Frame.prevProj * u_Frame.prevView * worldPos;
  outPosition = vec3(worldPos);

  mat3 normalMatrix = transpose(inverse(mat3(worldMatrix)));

  vec3 N = normalize(normalMatrix * inNormal);
  vec3 T = normalize(normalMatrix * inTangent.xyz);

  T = normalize(T - N * dot(T, N));
  vec3 B = cross(N, T) * inTangent.w;

  outNormal = N;
  outTBN = mat3(T, B, N);
#endif
}
