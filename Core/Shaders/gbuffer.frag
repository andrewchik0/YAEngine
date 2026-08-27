layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inPosition;
layout(location = 3) in mat3 inTBN;
layout(location = 6) in vec4 inCurClipPos;
layout(location = 7) in vec4 inPrevClipPos;

#include "common.glsl"

#include "utils.glsl"

#include "material.glsl"

layout(location = 0) out vec4 outGBuffer0;
layout(location = 1) out vec4 outGBuffer1;
layout(location = 2) out vec2 outVelocity;

#include "octahedron.glsl"

void main() {
  float gamma = u_Frame.gamma;

  vec2 uv = materialUV(inTexCoord);

  float hasAlbedoTexture = float(u_Material.textureMask & 1);
  vec4 albedoTex = mix(vec4(1.0), texture(baseColorTexture, uv), hasAlbedoTexture);
  vec4 albedo = vec4(u_Material.albedo, 1.0) * albedoTex;

#ifdef ALPHA_TEST
  if (albedo.a < 0.5)
    discard;
#endif

  albedo = vec4(pow(albedo.rgb, vec3(gamma)), albedo.a);

  float hasNormalMap = float((u_Material.textureMask >> 5) & 1);
  vec3 n_ts = sampleMaterialNormal(uv);
  vec3 normal = mix(inNormal, normalize(inTBN * n_ts), hasNormalMap);

  float hasMetallicTexture = float((u_Material.textureMask >> 1) & 1);
  vec4 metallicSample = texture(metallicTexture, uv);
  float metallic = u_Material.metallic * mix(1.0, metallicSample.b, hasMetallicTexture);

  float combinedTextures = float((u_Material.textureMask >> 8) & 1);

  float hasRoughnessTexture = float((u_Material.textureMask >> 2) & 1);
  float roughness = u_Material.roughness * mix(
    mix(1.0, texture(roughnessTexture, uv).r, hasRoughnessTexture),
    metallicSample.g,
    combinedTextures
  );

  vec2 velocity = computeVelocity(inCurClipPos, inPrevClipPos);

  // GBuffer1: octahedron-encoded normal (10+10 bit) + roughness (10 bit) + shadingModel (2 bit)
  vec2 octNorm = octEncode(normal) * 0.5 + 0.5;

  // The emissive decision is per texel, not per material: a sign is one mesh whose plate
  // stays PBR while its lettering emits, and splitting it into two materials to express
  // that would mean re-authoring every imported asset.
  vec3 emissive = materialEmissiveShading() ? materialEmissive(uv) : vec3(0.0);

  if (luminance(emissive) > EMISSIVE_SHADING_CUTOFF)
  {
    // Emission takes GBuffer0 whole, so metallic is gone. Roughness is forced to 1 to keep
    // SSR from tracing rays out of a self-lit texel - it rejects anything above MAX_ROUGHNESS.
    outGBuffer0 = encodeEmissive(emissive);
    outGBuffer1 = vec4(octNorm, 1.0, SHADING_MODEL_EMISSIVE);
  }
  else
  {
    // GBuffer0: albedo.rgb + metallic
    outGBuffer0 = vec4(albedo.rgb, metallic);
    outGBuffer1 = vec4(octNorm, roughness, SHADING_MODEL_PBR);
  }

  outVelocity = velocity;
}
