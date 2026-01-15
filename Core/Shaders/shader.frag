#version 450

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inPosition;
layout(location = 3) in mat3 inTBN;

layout(set = 0, binding = 0) uniform PerFrameUBO {
  mat4 view;
  mat4 proj;
  vec3 cameraPosition;
  float time;
  vec3 cameraDirection;
  float gamma;
  float exposure;
  int currentTexture;
  float near;
  float far;
} u_Data;

struct Light
{
  vec3 position;
  float cutOff;
  vec3 color;
  float outerCutOff;
};

layout(set = 2, binding = 0) readonly buffer Lights
{
  Light lights[2];
  int lightsCount;
} u_Lights;

layout(set = 1, binding = 0) uniform PerMaterialUBO {
  vec3 albedo;
  float roughness;
  vec3 emissivity;
  float specular;
  float metallic;
  int textureMask;
  int sg;
} u_Material;
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

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;

#include "post.glsl"
#include "pbr.glsl"

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
  return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
  float exposure = u_Data.exposure;
  float gamma = u_Data.gamma;

  float hasNormalMap = float((u_Material.textureMask >> 5) & 1);
  vec3 n_ts = texture(normalTexture, inTexCoord).rgb * 2.0 - 1.0;
  vec3 normal = mix(inNormal, normalize(inTBN * n_ts), hasNormalMap);

  float hasAlbedoTexture = float(u_Material.textureMask & 1);
  vec4 albedo = mix(vec4(u_Material.albedo, 1.0), texture(baseColorTexture, inTexCoord), hasAlbedoTexture);
  albedo = vec4(pow(albedo.rgb, vec3(gamma)), albedo.a);

  float hasMetallicTexture = float((u_Material.textureMask >> 5) & 1);
  float metallic = mix(u_Material.metallic, texture(metallicTexture, inTexCoord).b, hasMetallicTexture);

  float combinedTextures = float((u_Material.textureMask >> 8) & 1);

  float hasRoughnessTexture = float((u_Material.textureMask >> 2) & 1);
  float roughness = mix(
    mix(u_Material.roughness, texture(roughnessTexture, inTexCoord).r, hasRoughnessTexture),
    texture(metallicTexture, inTexCoord).g,
    combinedTextures
  );

  vec3 viewVec = normalize(u_Data.cameraPosition - inPosition);

  float NdotV = clamp(max(dot(normal, viewVec), -dot(normal, viewVec)), 0.01, 0.99);
  vec3 f0 = mix(vec3(0.04), albedo.rgb, metallic);

  vec3 resultColor = vec3(0);

  const int lightCount = 0;
  for (int i = 0; i < lightCount; ++i)
  {
    vec3 lightDir = normalize(u_Lights.lights[i].position - inPosition);
    vec3 lightColor = u_Lights.lights[i].color / (length(lightDir) * length(lightDir));
    vec3 halfWayVec = normalize(viewVec + lightDir);

    vec3 spotDir = normalize(vec3(0, 1, 0));
    float cutOff = cos(radians(u_Lights.lights[i].cutOff));
    float outerCutOff = cos(radians(u_Lights.lights[i].outerCutOff));

    float theta = dot(lightDir, normalize(spotDir));
    float epsilon = (cutOff - outerCutOff);
    float intensity = clamp((theta - outerCutOff) / epsilon, 0.0, 1.0);

    resultColor += PBR(lightDir, lightColor, halfWayVec, viewVec, normal, f0, dot(normal, viewVec), metallic, albedo.rgb, roughness * roughness, vec3(0.0)) * intensity;
  }

  vec3 F = fresnelSchlickRoughness(NdotV, f0, roughness);

  vec3 kD = 1.0 - F;
  kD *= (1 - metallic);

  vec3 irradiance = texture(irradianceCubemap, normal).rgb;
  vec3 diffuse = irradiance * albedo.rgb;

  vec3 R = reflect(-viewVec, normal);
  const float MAX_REFLECTION_LOD = 9.0;
  vec3 prefilteredColor = textureLod(prefilterTexture, R, roughness * MAX_REFLECTION_LOD).rgb;
  vec2 brdf = texture(brdfTexture, vec2(NdotV, clamp(roughness, 0.01, .99))).rg;
  vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

  vec3 ambient = kD * diffuse + specular * (1.0 - clamp(roughness, 0, 0.8));
  resultColor += max(ambient, vec3(0));

  vec3 mapped = resultColor * exposure;

  mapped = pow(mapped, vec3(1.0 / gamma));

  mapped = ACESFilm(mapped);
//  vec3 finalColor = pow(mapped, vec3(1.0 / gamma));

  if (albedo.a < 0.5)
  {
    discard;
  }
  else
  {
    outColor = vec4(vec3(mapped), 1.0);
    outNormal = vec4(normal, 1.0);
  }

  if (bool(u_Data.currentTexture))
  {
    switch (u_Data.currentTexture)
    {
    case 1:
      outColor = vec4(albedo.rgb, 1.0);
      break;
    case 2:
      outColor = vec4(vec3(metallic), 1.0);
      break;
    case 3:
      outColor = vec4(vec3(roughness), 1.0);
      break;
    case 4:
      outColor = vec4(vec3(normal), 1.0);
      break;
    }
  }
}
