#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "raytracing_common.glsl"

// Location 1, the path tracer's shadow payload. Location 0 belongs to pathtrace.rmiss and
// the surface hits; keeping the two apart is what lets a shadow ray be traced from inside
// the bounce loop without disturbing the surface payload the loop is still holding.
layout(location = 1) rayPayloadInEXT ShadowRayPayload payload;

// Reaching this shader IS the answer. The ray was traced with TerminateOnFirstHit and
// SkipClosestHitShader, so no other shader of the pipeline can run for it except
// pt_shadow.rahit, which only ever rejects candidates - the texels the alpha cutout removes, and
// every dielectric once its transmittance is in the payload. Nothing accepted means the light is
// visible, as much of it as the transmittance says.
void main()
{
  payload.visible = 1u;
}
