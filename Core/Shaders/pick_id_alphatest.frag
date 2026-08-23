layout(location = 0) in vec2 inTexCoord;
layout(location = 1) flat in uint inPickId;

layout(set = 1, binding = 1) uniform sampler2D baseColorTexture;

layout(location = 0) out uint outPickId;

void main()
{
  // Same cutoff as alphatest_discard.frag. The depth test alone cannot stand in for it:
  // a punched-out fragment that sits in front of whatever wrote the depth still passes
  // GEQUAL, and a leaf quad would catch clicks over its transparent half.
  if (texture(baseColorTexture, inTexCoord).a < 0.5)
    discard;

  outPickId = inPickId;
}
