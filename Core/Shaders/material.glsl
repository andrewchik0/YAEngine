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

// glTF semantics: emissiveFactor * emissiveTexture. emissivity already carries the
// KHR_materials_emissive_strength multiplier, folded in on the CPU side.
vec3 materialEmissive(vec2 uv)
{
  float hasEmissive = float((u_Material.textureMask >> 4) & 1);
  return u_Material.emissivity * mix(vec3(1.0), texture(emissiveTexture, uv).rgb, hasEmissive);
}

// Bit 10 opts the material into emissive shading: bright texels drop their PBR response
// and are written to the G-buffer as pure emission instead.
bool materialEmissiveShading()
{
  return ((u_Material.textureMask >> 10) & 1) != 0;
}
