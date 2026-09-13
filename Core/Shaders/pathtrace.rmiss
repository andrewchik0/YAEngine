#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "raytracing_common.glsl"

layout(location = 0) rayPayloadInEXT RayTracingPayload payload;

// The miss color itself is not written here: the ray generation shader owns the pixel, and
// a payload flag is all a bounce loop will need to know the ray left the scene.
void main()
{
  payload.hit = 0u;
}
