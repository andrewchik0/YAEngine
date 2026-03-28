layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inPosition;
layout(location = 3) in mat3 inTBN;
layout(location = 6) in vec4 inCurClipPos;
layout(location = 7) in vec4 inPrevClipPos;

#include "common.glsl"

#include "material.glsl"

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec2 outMaterial;
layout(location = 3) out vec4 outAlbedo;
layout(location = 4) out vec2 outVelocity;

#include "pbr.glsl"

void main() {
  float gamma = u_Frame.gamma;

  float hasAlbedoTexture = float(u_Material.textureMask & 1);
  vec4 albedo = mix(vec4(u_Material.albedo, 1.0), texture(baseColorTexture, inTexCoord), hasAlbedoTexture);

  albedo = vec4(pow(albedo.rgb, vec3(gamma)), albedo.a);

  float hasNormalMap = float((u_Material.textureMask >> 5) & 1);
  vec3 n_ts = texture(normalTexture, inTexCoord).rgb * 2.0 - 1.0;
  vec3 normal = mix(inNormal, normalize(inTBN * n_ts), hasNormalMap);

  float hasMetallicTexture = float((u_Material.textureMask >> 1) & 1);
  vec4 metallicSample = texture(metallicTexture, inTexCoord);
  float metallic = mix(u_Material.metallic, metallicSample.b, hasMetallicTexture);

  float combinedTextures = float((u_Material.textureMask >> 8) & 1);

  float hasRoughnessTexture = float((u_Material.textureMask >> 2) & 1);
  float roughness = mix(
    mix(u_Material.roughness, texture(roughnessTexture, inTexCoord).r, hasRoughnessTexture),
    metallicSample.g,
    combinedTextures
  );

  vec3 viewVec = normalize(u_Frame.cameraPosition - inPosition);

  float NdotV = clamp(abs(dot(normal, viewVec)), 0.01, 0.99);
  vec3 f0 = mix(vec3(0.04), albedo.rgb, metallic);

  // IBL ambient
  vec3 F = fresnelSchlickRoughness(NdotV, f0, roughness);

  vec3 kD = 1.0 - F;
  kD *= (1.0 - metallic);

  vec3 irradiance = texture(irradianceCubemap, normal).rgb;
  vec3 diffuse = irradiance * albedo.rgb;

  vec3 R = reflect(-viewVec, normal);
  const float MAX_REFLECTION_LOD = 9.0;
  vec3 prefilteredColor = textureLod(prefilterTexture, R, roughness * MAX_REFLECTION_LOD).rgb;
  vec2 brdf = texture(brdfTexture, vec2(NdotV, clamp(roughness, 0.01, 0.99))).rg;
  vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

  vec3 ambient = kD * diffuse + specular * (1.0 - clamp(roughness, 0.0, 0.8));
  vec3 resultColor = max(ambient, vec3(0.0));

  vec2 curNDC = inCurClipPos.xy / inCurClipPos.w;
  vec2 prevNDC = inPrevClipPos.xy / inPrevClipPos.w;
  vec2 velocity = (curNDC - prevNDC) * 0.5;

  outColor = vec4(resultColor, 1.0);
  outNormal = vec4(normal, 1.0);
  outMaterial = vec2(roughness, metallic);
  outAlbedo = vec4(albedo.rgb, 1.0);
  outVelocity = velocity;
}
