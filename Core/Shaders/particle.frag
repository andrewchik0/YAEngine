#version 450

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 1) uniform sampler2D u_ParticleTexture;

void main()
{
  vec4 sampled = texture(u_ParticleTexture, inUV);
  float alpha = sampled.a * inColor.a;
  if (alpha < 0.001)
    discard;

  // Premultiplied output: additive blending (ONE, ONE) expects color pre-weighted by alpha.
  vec3 rgb = sampled.rgb * inColor.rgb * alpha;
  outColor = vec4(rgb, alpha);
}
