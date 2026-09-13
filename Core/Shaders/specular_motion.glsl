#ifndef SPECULAR_MOTION_GLSL
#define SPECULAR_MOTION_GLSL

// Requires raytracing_common.glsl (for u_Frame and RayTracingInstanceRecord) to be included
// before this file.
//
// Specular motion vectors for ray reconstruction. A mirror reflection is seen as a virtual
// image: the reflected point mirrored across the reflector's tangent plane, which puts it on the
// camera ray through the pixel. Where that image was last frame follows from moving the
// reflected point and the plane back by their instances' worldToPrevWorld and mirroring again.
// Every result is in MainVelocity's convention - see computeVelocity in utils.glsl - because the
// two Streamline tags share one sl::Constants::mvecScale.

#include "reprojection.glsl"

const vec4 WORLD_TO_PREV_WORLD_IDENTITY[3] = vec4[3](
  vec4(1.0, 0.0, 0.0, 0.0),
  vec4(0.0, 1.0, 0.0, 0.0),
  vec4(0.0, 0.0, 1.0, 0.0));

vec3 applyWorldToPrevWorldPoint(vec4 rows[3], vec3 point)
{
  vec4 homogeneous = vec4(point, 1.0);
  return vec3(dot(rows[0], homogeneous), dot(rows[1], homogeneous), dot(rows[2], homogeneous));
}

// Linear part only, renormalized. Exact for rigid motion under a uniform scale, which is what
// moving props do; a non-uniform scale change would need the inverse transpose instead.
vec3 applyWorldToPrevWorldDirection(vec4 rows[3], vec3 direction)
{
  return normalize(vec3(dot(rows[0].xyz, direction), dot(rows[1].xyz, direction),
    dot(rows[2].xyz, direction)));
}

vec3 mirrorPoint(vec3 point, vec3 planePoint, vec3 planeNormal)
{
  return point - 2.0 * dot(point - planePoint, planeNormal) * planeNormal;
}

vec3 mirrorDirection(vec3 direction, vec3 normal)
{
  return reflect(direction, normal);
}

// currentNDC must already carry the jitter, as backgroundVelocity's does. A point that was behind
// the previous camera has no history, which backgroundVelocity reports as (2, 2).
vec2 specularMotionFromPrevClip(vec4 prevClip, vec2 currentNDC)
{
  if (prevClip.w <= 0.0)
    return vec2(2.0);

  return (currentNDC - prevClip.xy / prevClip.w) * 0.5;
}

#endif
