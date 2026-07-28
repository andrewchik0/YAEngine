// Requires common.glsl to be included before this file

float linearizeDepth(float d)
{
  return u_Frame.nearPlane * u_Frame.farPlane / (u_Frame.farPlane - d * (u_Frame.farPlane - u_Frame.nearPlane));
}

vec3 reconstructViewPos(vec2 screenUV, float depth)
{
  vec2 ndc = screenUV * 2.0 - 1.0;
  vec4 clipPos = vec4(ndc, depth, 1.0);
  vec4 viewPos = u_Frame.invProj * clipPos;
  return viewPos.xyz / viewPos.w;
}

// Screen-space motion vector in UV units.
// curClipPos comes from the jittered projection while prevProj is stored unjittered, so the
// jitter is added back here: the vector must describe geometric motion only. TAA transports a
// per-pixel history anchor along it, so a static camera has to yield exactly zero - otherwise
// the history is resampled every frame by the sub-pixel offset and the image crawls.
// proj[2][0..1] += jitter shifts NDC by exactly -jitter at any depth, hence the plus.
vec2 computeVelocity(vec4 curClipPos, vec4 prevClipPos)
{
  vec2 curNDC = curClipPos.xy / curClipPos.w + vec2(u_Frame.jitterX, u_Frame.jitterY);
  vec2 prevNDC = prevClipPos.xy / prevClipPos.w;
  return (curNDC - prevNDC) * 0.5;
}
