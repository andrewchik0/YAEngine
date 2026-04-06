layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inTangent;

#include "../Shared/ShadowData.h"
layout(set = 0, binding = 0) uniform ShadowCascadesBlock { ShadowBuffer u_Shadow; };

#ifdef INSTANCED
layout(set = 1, binding = 0) readonly buffer Instances
{
  mat4 data[];
} instances;
#endif

layout(push_constant) uniform PushConstants
{
  mat4 world;
  int offset;
  int shadowMatrixIndex;
} pc;

mat4 getShadowViewProj(int index)
{
  if (index < SHADOW_SPOT_MATRIX_OFFSET)
    return u_Shadow.cascades[index].viewProj;

  if (index < SHADOW_POINT_MATRIX_OFFSET)
    return u_Shadow.spotShadows[index - SHADOW_SPOT_MATRIX_OFFSET].viewProj;

  int pointLocal = index - SHADOW_POINT_MATRIX_OFFSET;
  return u_Shadow.pointShadows[pointLocal / 6].faceViewProj[pointLocal % 6];
}

void main()
{
#ifdef INSTANCED
  mat4 worldMatrix = pc.world * instances.data[gl_InstanceIndex + pc.offset];
#else
  mat4 worldMatrix = pc.world;
#endif

  vec4 worldPos = worldMatrix * vec4(inPosition, 1.0);
  gl_Position = getShadowViewProj(pc.shadowMatrixIndex) * worldPos;
}
