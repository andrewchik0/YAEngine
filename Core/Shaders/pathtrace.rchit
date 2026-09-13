#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "raytracing_common.glsl"

layout(location = 0) rayPayloadInEXT RayTracingPayload payload;

// The two barycentrics the fixed-function triangle intersection reports. The third is
// 1 - x - y, which interpolateTexCoord reconstructs.
hitAttributeEXT vec2 hitBarycentrics;

// Records what was hit and nothing more: the shading lives in pathtrace.rgen so the path
// tracer's bounce loop can grow around it. gl_ObjectToWorldEXT is the same mat4x3 a ray
// query returns from rayQueryGetIntersectionObjectToWorldEXT.
void main()
{
  payload.barycentrics = hitBarycentrics;
  payload.instanceIndex = uint(gl_InstanceCustomIndexEXT);
  payload.primitiveIndex = uint(gl_PrimitiveID);
  payload.hit = 1u;
  payload.hitT = gl_HitTEXT;
  payload.objectToWorld = gl_ObjectToWorldEXT;
}
