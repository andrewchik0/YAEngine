layout(location = 0) in vec2 inTexCoord;

#include "material_uniforms.glsl"

layout(set = 1, binding = 1) uniform sampler2D baseColorTexture;

void main()
{
  float alpha = texture(baseColorTexture, materialUV(inTexCoord)).a;
  if (alpha < 0.5)
    discard;
}
