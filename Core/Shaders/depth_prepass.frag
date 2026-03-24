#version 450

layout(location = 0) in vec2 uv;

layout(set = 1, binding = 1) uniform sampler2D baseColorTexture;

void main()
{
  float alpha = texture(baseColorTexture, uv).a;
  if (alpha < 0.5)
    discard;
}
