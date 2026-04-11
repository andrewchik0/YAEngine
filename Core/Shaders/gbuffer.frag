layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inPosition;
layout(location = 3) in mat3 inTBN;
layout(location = 6) in vec4 inCurClipPos;
layout(location = 7) in vec4 inPrevClipPos;

#include "common.glsl"

#include "material.glsl"

layout(location = 0) out vec4 outGBuffer0;
layout(location = 1) out vec4 outGBuffer1;
layout(location = 2) out vec2 outVelocity;

#include "octahedron.glsl"

void main() {
  float gamma = u_Frame.gamma;

  float hasAlbedoTexture = float(u_Material.textureMask & 1);
  vec4 albedoTex = mix(vec4(1.0), texture(baseColorTexture, inTexCoord), hasAlbedoTexture);
  vec4 albedo = vec4(u_Material.albedo, 1.0) * albedoTex;

  albedo = vec4(pow(albedo.rgb, vec3(gamma)), albedo.a);

  float hasNormalMap = float((u_Material.textureMask >> 5) & 1);
  vec3 n_ts = texture(normalTexture, inTexCoord).rgb * 2.0 - 1.0;
  vec3 normal = mix(inNormal, normalize(inTBN * n_ts), hasNormalMap);

  float hasMetallicTexture = float((u_Material.textureMask >> 1) & 1);
  vec4 metallicSample = texture(metallicTexture, inTexCoord);
  float metallic = u_Material.metallic * mix(1.0, metallicSample.b, hasMetallicTexture);

  float combinedTextures = float((u_Material.textureMask >> 8) & 1);

  float hasRoughnessTexture = float((u_Material.textureMask >> 2) & 1);
  float roughness = u_Material.roughness * mix(
    mix(1.0, texture(roughnessTexture, inTexCoord).r, hasRoughnessTexture),
    metallicSample.g,
    combinedTextures
  );

  vec2 curNDC = inCurClipPos.xy / inCurClipPos.w;
  vec2 prevNDC = inPrevClipPos.xy / inPrevClipPos.w;
  vec2 velocity = (curNDC - prevNDC) * 0.5;

  // GBuffer0: albedo.rgb + metallic
  outGBuffer0 = vec4(albedo.rgb, metallic);

  // GBuffer1: octahedron-encoded normal (10+10 bit) + roughness (10 bit) + shadingModel (2 bit)
  vec2 octNorm = octEncode(normal) * 0.5 + 0.5;
  float shadingModelPBR = 0.0;
  outGBuffer1 = vec4(octNorm, roughness, shadingModelPBR);

  outVelocity = velocity;
}
