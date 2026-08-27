#ifndef REPROJECTION_GLSL
#define REPROJECTION_GLSL

// Requires common.glsl (for u_Frame) to be included before this file.
// Shared by the TAA resolve and the SSGI radiance prefilter, which must agree
// on the reprojection rule to the letter - a divergence would make SSGI gather
// radiance from pixels TAA never validated.

// Background pixels are never rasterized, so the velocity buffer still holds its clear value
// there. Reproject the view ray instead: an infinitely distant sample follows camera rotation
// but not translation, which is why the direction is transformed with w = 0.
// The result is unjittered like the geometry path - see computeVelocity in utils.glsl.
vec2 backgroundVelocity(vec2 ndc)
{
  vec4 viewPos = u_Frame.invProj * vec4(ndc, 1.0, 1.0);
  vec3 worldDir = mat3(u_Frame.invView) * viewPos.xyz;

  // Parenthesised: without them this is mat4 * mat4 per fragment before the mat-vec
  vec4 prevClip = u_Frame.prevProj * (u_Frame.prevView * vec4(worldDir, 0.0));

  // Direction was behind the previous camera: no history exists, push the lookup out of bounds
  if (prevClip.w <= 0.0)
    return vec2(2.0);

  vec2 curNDC = ndc + vec2(u_Frame.jitterX, u_Frame.jitterY);
  return (curNDC - prevClip.xy / prevClip.w) * 0.5;
}

#endif
