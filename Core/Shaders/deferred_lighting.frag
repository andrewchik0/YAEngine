layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

#include "common.glsl"
#include "utils.glsl"
#include "octahedron.glsl"
#include "pbr.glsl"

// G-buffer (set 1)
layout(set = 1, binding = 0) uniform sampler2D gbuffer0Texture;
layout(set = 1, binding = 1) uniform sampler2D gbuffer1Texture;
layout(set = 1, binding = 2) uniform sampler2D depthTexture;
layout(set = 1, binding = 3) uniform sampler2D ssaoTexture;

// Lights (set 2)
#include "../Shared/LightData.h"
layout(std430, set = 2, binding = 0) readonly buffer LightBufferSSBO
{
  LightBuffer u_Lights;
};

// IBL (set 3)
layout(set = 3, binding = 0) uniform samplerCube irradianceCubemap;
layout(set = 3, binding = 1) uniform samplerCube prefilterTexture;
layout(set = 3, binding = 2) uniform sampler2D brdfTexture;
layout(set = 3, binding = 3) uniform samplerCube skyboxCubemap;

const float DEPTH_EPSILON = 0.9999;
const int SHADING_PBR = 0;
const int SHADING_UNLIT = 1;

void main()
{
  float depth = texture(depthTexture, uv).r;

  // Sky pixels — sample skybox cubemap
  if (depth >= DEPTH_EPSILON)
  {
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 clipPos = vec4(ndc, 1.0, 1.0);
    vec4 viewPos = u_Frame.invProj * clipPos;
    vec3 worldDir = normalize(mat3(u_Frame.invView) * viewPos.xyz);
    vec3 skyColor = texture(skyboxCubemap, worldDir).rgb;
    outColor = vec4(skyColor, 1.0);
    return;
  }

  // Read G-buffer
  vec4 gb0 = texture(gbuffer0Texture, uv);
  vec4 gb1 = texture(gbuffer1Texture, uv);

  vec3 albedo = gb0.rgb;
  float metallic = gb0.a;

  vec3 normal = octDecode(gb1.rg * 2.0 - 1.0);
  float roughness = gb1.b;
  int shadingModel = int(gb1.a * 3.0 + 0.5);

  // Unlit — output albedo directly
  if (shadingModel == SHADING_UNLIT)
  {
    outColor = vec4(albedo, 1.0);
    return;
  }

  // Reconstruct world position from depth
  vec3 viewPos = reconstructViewPos(uv, depth);
  vec3 worldPos = (u_Frame.invView * vec4(viewPos, 1.0)).xyz;

  // SSAO
  float ao = texture(ssaoTexture, uv).r;
  if (u_Frame.ssaoEnabled == 0)
    ao = 1.0;

  // PBR IBL lighting (moved from old shader.frag)
  vec3 viewVec = normalize(u_Frame.cameraPosition - worldPos);
  float NdotV = clamp(abs(dot(normal, viewVec)), 0.01, 0.99);
  vec3 f0 = mix(vec3(0.04), albedo, metallic);

  vec3 F = fresnelSchlickRoughness(NdotV, f0, roughness);

  vec3 kD = 1.0 - F;
  kD *= (1.0 - metallic);

  vec3 irradiance = texture(irradianceCubemap, normal).rgb;
  vec3 diffuse = irradiance * albedo;

  vec3 R = reflect(-viewVec, normal);
  const float MAX_REFLECTION_LOD = 9.0;
  vec3 prefilteredColor = textureLod(prefilterTexture, R, roughness * MAX_REFLECTION_LOD).rgb;
  vec2 brdf = texture(brdfTexture, vec2(NdotV, clamp(roughness, 0.01, 0.99))).rg;
  vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

  vec3 ambient = kD * diffuse + specular * (1.0 - clamp(roughness, 0.0, 0.8));

  // Analytical lights
  vec3 Lo = vec3(0.0);
  float alpha = roughness * roughness;

  // Directional light
  {
    vec3 L = normalize(-u_Lights.directional.directionIntensity.xyz);
    float intensity = u_Lights.directional.directionIntensity.w;
    vec3 radiance = u_Lights.directional.colorPad.rgb * intensity;
    Lo += evaluateDirectLight(normal, viewVec, L, radiance, albedo, metallic, alpha, f0, NdotV);
  }

  // Point lights
  for (int i = 0; i < u_Lights.pointLightCount; i++)
  {
    vec3 lightPos = u_Lights.pointLights[i].positionRadius.xyz;
    float lightRadius = u_Lights.pointLights[i].positionRadius.w;

    vec3 L = lightPos - worldPos;
    float dist = length(L);
    if (dist > lightRadius) continue;
    L /= dist;

    float att = 1.0 - (dist * dist) / (lightRadius * lightRadius);
    att = att * att;
    vec3 radiance = u_Lights.pointLights[i].colorIntensity.rgb * u_Lights.pointLights[i].colorIntensity.w * att;

    Lo += evaluateDirectLight(normal, viewVec, L, radiance, albedo, metallic, alpha, f0, NdotV);
  }

  // Spot lights
  for (int i = 0; i < u_Lights.spotLightCount; i++)
  {
    vec3 lightPos = u_Lights.spotLights[i].positionRadius.xyz;
    float lightRadius = u_Lights.spotLights[i].positionRadius.w;

    vec3 L = lightPos - worldPos;
    float dist = length(L);
    if (dist > lightRadius) continue;
    L /= dist;

    vec3 lightDir = u_Lights.spotLights[i].directionInnerCone.xyz;
    float innerCos = u_Lights.spotLights[i].directionInnerCone.w;
    float outerCos = u_Lights.spotLights[i].colorOuterCone.w;
    float theta = dot(L, -lightDir);
    float spotFactor = clamp((theta - outerCos) / (innerCos - outerCos), 0.0, 1.0);

    float att = 1.0 - (dist * dist) / (lightRadius * lightRadius);
    att = att * att;
    vec3 radiance = u_Lights.spotLights[i].colorOuterCone.rgb * u_Lights.spotLights[i].intensityPad.x * att * spotFactor;

    Lo += evaluateDirectLight(normal, viewVec, L, radiance, albedo, metallic, alpha, f0, NdotV);
  }

  vec3 resultColor = max(ambient * ao + Lo, vec3(0.0));

  outColor = vec4(resultColor, 1.0);
}
