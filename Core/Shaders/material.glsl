#include "../Shared/MaterialUniforms.h"
#include "normal_map.glsl"
layout(set = 1, binding = 0) uniform MaterialUniformsBlock { MaterialUniforms u_Material; };
layout(set = 1, binding = 1) uniform sampler2D baseColorTexture;
layout(set = 1, binding = 2) uniform sampler2D metallicTexture;
layout(set = 1, binding = 3) uniform sampler2D roughnessTexture;
layout(set = 1, binding = 4) uniform sampler2D specularTexture;
layout(set = 1, binding = 5) uniform sampler2D emissiveTexture;
layout(set = 1, binding = 6) uniform sampler2D normalTexture;
layout(set = 1, binding = 7) uniform sampler2D heightTexture;
layout(set = 1, binding = 8) uniform samplerCube prefilterTexture;
layout(set = 1, binding = 9) uniform sampler2D brdfTexture;
layout(set = 1, binding = 10) uniform samplerCube irradianceCubemap;

// Bit 9 of textureMask marks a two-channel normal map, everything else keeps the
// stored blue channel.
vec3 sampleMaterialNormal(vec2 uv)
{
  return decodeNormalMap(texture(normalTexture, uv).rgb,
    float((u_Material.textureMask >> 9) & 1));
}
