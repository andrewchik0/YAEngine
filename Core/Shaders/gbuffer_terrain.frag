layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inPosition;
layout(location = 3) in mat3 inTBN;
layout(location = 6) in vec4 inCurClipPos;
layout(location = 7) in vec4 inPrevClipPos;

#include "common.glsl"

#include "terrain_material.glsl"

layout(location = 0) out vec4 outGBuffer0;
layout(location = 1) out vec4 outGBuffer1;
layout(location = 2) out vec2 outVelocity;

#include "octahedron.glsl"

void main() {
  float gamma = u_Frame.gamma;

  // Slope-based blend factor: 0 = flat (layer0/sand), 1 = steep (layer1/rock)
  float slope = 1.0 - abs(dot(normalize(inNormal), vec3(0.0, 1.0, 0.0)));
  float blend = smoothstep(u_Terrain.slopeStart, u_Terrain.slopeEnd, slope);

  vec2 uv0 = inTexCoord;
  vec2 uv1 = inTexCoord * u_Terrain.layer1UvScale / u_Terrain.layer0UvScale;

  // Layer 0 (sand)
  float hasL0Albedo = float(u_Terrain.textureMask & 1);
  vec4 albedo0Tex = mix(vec4(1.0), texture(layer0AlbedoTexture, uv0), hasL0Albedo);
  vec4 albedo0 = vec4(u_Terrain.layer0Albedo, 1.0) * albedo0Tex;

  float hasL0Normal = float((u_Terrain.textureMask >> 1) & 1);
  vec3 n0_ts = texture(layer0NormalTexture, uv0).rgb * 2.0 - 1.0;

  float hasL0Roughness = float((u_Terrain.textureMask >> 2) & 1);
  float roughness0 = u_Terrain.layer0Roughness * mix(1.0, texture(layer0RoughnessTexture, uv0).r, hasL0Roughness);

  float hasL0Metallic = float((u_Terrain.textureMask >> 3) & 1);
  float metallic0 = u_Terrain.layer0Metallic * mix(1.0, texture(layer0MetallicTexture, uv0).b, hasL0Metallic);

  // Layer 1 (rock)
  float hasL1Albedo = float((u_Terrain.textureMask >> 4) & 1);
  vec4 albedo1Tex = mix(vec4(1.0), texture(layer1AlbedoTexture, uv1), hasL1Albedo);
  vec4 albedo1 = vec4(u_Terrain.layer1Albedo, 1.0) * albedo1Tex;

  float hasL1Normal = float((u_Terrain.textureMask >> 5) & 1);
  vec3 n1_ts = texture(layer1NormalTexture, uv1).rgb * 2.0 - 1.0;

  float hasL1Roughness = float((u_Terrain.textureMask >> 6) & 1);
  float roughness1 = u_Terrain.layer1Roughness * mix(1.0, texture(layer1RoughnessTexture, uv1).r, hasL1Roughness);

  float hasL1Metallic = float((u_Terrain.textureMask >> 7) & 1);
  float metallic1 = u_Terrain.layer1Metallic * mix(1.0, texture(layer1MetallicTexture, uv1).b, hasL1Metallic);

  // Blend layers
  vec4 albedo = mix(albedo0, albedo1, blend);
  albedo = vec4(pow(albedo.rgb, vec3(gamma)), albedo.a);

  vec3 n_ts = normalize(mix(n0_ts * hasL0Normal, n1_ts * hasL1Normal, blend));
  float hasAnyNormal = max(hasL0Normal, hasL1Normal);
  vec3 normal = mix(inNormal, normalize(inTBN * n_ts), hasAnyNormal);

  float roughness = mix(roughness0, roughness1, blend);
  float metallic = mix(metallic0, metallic1, blend);

  // Velocity
  vec2 curNDC = inCurClipPos.xy / inCurClipPos.w;
  vec2 prevNDC = inPrevClipPos.xy / inPrevClipPos.w;
  vec2 velocity = (curNDC - prevNDC) * 0.5;

  // GBuffer0: albedo.rgb + metallic
  outGBuffer0 = vec4(albedo.rgb, metallic);

  // GBuffer1: octahedron-encoded normal + roughness + shadingModel
  vec2 octNorm = octEncode(normal) * 0.5 + 0.5;
  float shadingModelPBR = 0.0;
  outGBuffer1 = vec4(octNorm, roughness, shadingModelPBR);

  outVelocity = velocity;
}
