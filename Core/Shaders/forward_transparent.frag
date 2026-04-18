layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inPosition;
layout(location = 3) in mat3 inTBN;
layout(location = 6) in vec4 inCurClipPos;
layout(location = 7) in vec4 inPrevClipPos;

#include "lighting_common.glsl"
#include "material.glsl"

layout(location = 0) out vec4 outColor;

void main()
{
  float gamma = u_Frame.gamma;

  float hasAlbedoTexture = float(u_Material.textureMask & 1);
  vec4 albedoTex = mix(vec4(1.0), texture(baseColorTexture, inTexCoord), hasAlbedoTexture);
  vec4 albedo = vec4(u_Material.albedo, 1.0) * albedoTex;

  float alpha = albedo.a * u_Material.opacity;
  if (alpha <= 0.001)
    discard;

  albedo.rgb = pow(albedo.rgb, vec3(gamma));

  float hasNormalMap = float((u_Material.textureMask >> 5) & 1);
  vec3 n_ts = texture(normalTexture, inTexCoord).rgb * 2.0 - 1.0;
  vec3 normal = mix(normalize(inNormal), normalize(inTBN * n_ts), hasNormalMap);

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

  vec3 worldPos = inPosition;
  vec3 viewPos = (u_Frame.view * vec4(worldPos, 1.0)).xyz;
  vec3 viewVec = normalize(u_Frame.cameraPosition - worldPos);
  float NdotV = clamp(abs(dot(normal, viewVec)), 0.01, 0.99);
  vec3 f0 = mix(vec3(0.04), albedo.rgb, metallic);
  vec3 R = reflect(-viewVec, normal);

  vec3 ambient = computeAmbientIBL(worldPos, normal, R, roughness, NdotV, f0, albedo.rgb, metallic);
  vec3 Lo = computeDirectLighting(worldPos, viewPos, normal, viewVec, albedo.rgb, metallic, roughness, f0, NdotV, ivec2(gl_FragCoord.xy));

  float hasEmissive = float((u_Material.textureMask >> 4) & 1);
  vec3 emissive = u_Material.emissivity + texture(emissiveTexture, inTexCoord).rgb * hasEmissive;

  vec3 resultColor = max(ambient + Lo + emissive, vec3(0.0));

  if (u_Frame.fogEnabled != 0)
  {
    vec3 toPixel = worldPos - u_Frame.cameraPosition;
    float rayLength = length(toPixel);
    float fogAmount = computeHeightFog(u_Frame.cameraPosition, toPixel / rayLength, rayLength);
    resultColor = mix(resultColor, u_Frame.fogColor, fogAmount);
  }

  outColor = vec4(resultColor, alpha);
}
