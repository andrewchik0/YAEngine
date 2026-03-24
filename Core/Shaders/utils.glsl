// Requires common.glsl to be included before this file

float linearizeDepth(float d)
{
  return u_Data.near * u_Data.far / (u_Data.far - d * (u_Data.far - u_Data.near));
}

vec3 reconstructViewPos(vec2 screenUV, float depth)
{
  vec2 ndc = screenUV * 2.0 - 1.0;
  vec4 clipPos = vec4(ndc, depth, 1.0);
  vec4 viewPos = u_Data.invProj * clipPos;
  return viewPos.xyz / viewPos.w;
}
