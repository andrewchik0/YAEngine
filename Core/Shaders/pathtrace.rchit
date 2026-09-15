#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "raytracing_common.glsl"

layout(location = 0) rayPayloadInEXT RayTracingPayload payload;

// The two barycentrics the fixed-function triangle intersection reports. The third is
// 1 - x - y, which interpolateTexCoord reconstructs.
hitAttributeEXT vec2 hitBarycentrics;

// Records what was hit and nothing more: the shading lives in the ray generation shader so
// the path tracer's bounce loop can grow around it.
void main()
{
  payload.barycentrics = hitBarycentrics;
  payload.instanceIndex = uint(gl_InstanceCustomIndexEXT);
  payload.primitiveIndex = uint(gl_PrimitiveID);
  payload.hit = 1u;
  // Taken as is: without the flip facing flag, which no instance sets, ray tracing calls the
  // side whose object space vertices run counterclockwise (right-handed) seen from the ray
  // origin front facing - the side COUNTER_CLOCKWISE rasterization keeps under the engine's
  // Y-flipped projection.
  payload.frontFacing = gl_HitKindEXT == gl_HitKindFrontFacingTriangleEXT ? 1u : 0u;
  payload.hitT = gl_HitTEXT;
  payload.objectToWorld = gl_ObjectToWorldEXT;
}
