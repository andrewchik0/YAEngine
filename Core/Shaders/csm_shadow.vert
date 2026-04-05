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
  int cascadeIndex;
} pc;

void main()
{
#ifdef INSTANCED
  mat4 worldMatrix = pc.world * instances.data[gl_InstanceIndex + pc.offset];
#else
  mat4 worldMatrix = pc.world;
#endif

  vec4 worldPos = worldMatrix * vec4(inPosition, 1.0);
  gl_Position = u_Shadow.cascades[pc.cascadeIndex].viewProj * worldPos;
}
