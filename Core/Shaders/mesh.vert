layout(location = 0) in vec3 inPosition;

#if !defined(DEPTH_ONLY) || defined(ALPHA_TEST)
layout(location = 1) in vec2 inTexCoord;
layout(location = 0) out vec2 outTexCoord;
#endif

#ifdef PICK_ID
// Location 1 is free in every pick permutation: the extra outputs below belong to the
// full permutation, which never defines PICK_ID.
layout(location = 1) flat out uint outPickId;
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
  #elif defined(TRANSPARENT)
layout(set = 4, binding = 0) readonly buffer Instances
  #else
layout(set = 2, binding = 0) readonly buffer Instances
  #endif
{
  mat4 data[];
} instances;
#endif

// Last frame's world matrix of this draw, the only source of object motion in the
// velocity buffer. Transparency never writes velocity, so it stays out of this.
#if !defined(DEPTH_ONLY) && !defined(TRANSPARENT)
  #ifdef INSTANCED
layout(set = 3, binding = 0) readonly buffer PrevWorldMatrices
  #else
layout(set = 2, binding = 0) readonly buffer PrevWorldMatrices
  #endif
{
  mat4 data[];
} prevWorld;
#endif

layout(push_constant) uniform PushConstants
{
  mat4 world;
  int offset;
#ifdef PICK_ID
  uint pickId;
#endif
#if !defined(DEPTH_ONLY) && !defined(TRANSPARENT)
  uint prevIndex;
#endif
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

#ifdef PICK_ID
  outPickId = pc.pickId;
#endif

#ifndef DEPTH_ONLY
  outCurClipPos = gl_Position;

#if defined(TRANSPARENT)
  vec4 prevWorldPos = worldPos;
#elif defined(INSTANCED)
  // Instances are treated as static, only the parent transform can move.
  mat4 prevWorldMatrix = prevWorld.data[pc.prevIndex] * instances.data[gl_InstanceIndex + pc.offset];
  vec4 prevWorldPos = prevWorldMatrix * vec4(inPosition, 1.0);
#else
  vec4 prevWorldPos = prevWorld.data[pc.prevIndex] * vec4(inPosition, 1.0);
#endif

  outPrevClipPos = u_Frame.prevProj * u_Frame.prevView * prevWorldPos;
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
