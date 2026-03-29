#version 450

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D u_GlyphTexture;

#include "../Shared/SpritePushConstants.h"

layout(push_constant) uniform PushConstantBlock { SpritePushConstants pc; };

float SampleBlurred(vec2 uv, float radius)
{
  const float PI = 3.14159265;
  float alpha = texture(u_GlyphTexture, uv).r;

  // Inner ring — 4 samples at half radius
  for (int i = 0; i < 4; i++)
  {
    float angle = float(i) * PI * 0.5;
    alpha += texture(u_GlyphTexture, uv + vec2(cos(angle), sin(angle)) * radius * 0.5).r;
  }

  // Outer ring — 8 samples at full radius
  for (int i = 0; i < 8; i++)
  {
    float angle = float(i) * PI * 0.25;
    alpha += texture(u_GlyphTexture, uv + vec2(cos(angle), sin(angle)) * radius).r;
  }

  return alpha / 13.0;
}

void main()
{
  if (pc.shadow.w > 0.5)
  {
    float alpha = SampleBlurred(inUV, pc.shadow.x);
    if (alpha < 0.005)
      discard;
    outColor = vec4(0.0, 0.0, 0.0, inColor.a * alpha * pc.shadow.z);
  }
  else
  {
    float alpha = texture(u_GlyphTexture, inUV).r;
    if (alpha < 0.01)
      discard;
    outColor = vec4(inColor.rgb, inColor.a * alpha);
  }
}
