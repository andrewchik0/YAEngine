#include "../Shared/TerrainMaterialUniforms.h"
layout(set = 1, binding = 0) uniform TerrainMaterialBlock { TerrainMaterialUniforms u_Terrain; };

layout(set = 1, binding = 1) uniform sampler2D layer0AlbedoTexture;
layout(set = 1, binding = 2) uniform sampler2D layer0NormalTexture;
layout(set = 1, binding = 3) uniform sampler2D layer0RoughnessTexture;
layout(set = 1, binding = 4) uniform sampler2D layer0MetallicTexture;

layout(set = 1, binding = 5) uniform sampler2D layer1AlbedoTexture;
layout(set = 1, binding = 6) uniform sampler2D layer1NormalTexture;
layout(set = 1, binding = 7) uniform sampler2D layer1RoughnessTexture;
layout(set = 1, binding = 8) uniform sampler2D layer1MetallicTexture;

layout(set = 1, binding = 9)  uniform sampler2D layer2AlbedoTexture;
layout(set = 1, binding = 10) uniform sampler2D layer2NormalTexture;
layout(set = 1, binding = 11) uniform sampler2D layer2RoughnessTexture;
layout(set = 1, binding = 12) uniform sampler2D layer2MetallicTexture;

struct LayerSample {
  vec4 albedo;
  vec3 normalTS;
  float roughness;
  float metallic;
};

LayerSample sampleLayer0(vec2 uv) {
  LayerSample s;
  float hasA = float(u_Terrain.textureMask & 1);
  vec4 aTex = mix(vec4(1.0), texture(layer0AlbedoTexture, uv), hasA);
  s.albedo = vec4(u_Terrain.layer0Albedo, 1.0) * aTex;

  float hasN = float((u_Terrain.textureMask >> 1) & 1);
  s.normalTS = mix(vec3(0.0, 0.0, 1.0), texture(layer0NormalTexture, uv).rgb * 2.0 - 1.0, hasN);

  float hasR = float((u_Terrain.textureMask >> 2) & 1);
  s.roughness = u_Terrain.layer0Roughness * mix(1.0, texture(layer0RoughnessTexture, uv).r, hasR);

  float hasM = float((u_Terrain.textureMask >> 3) & 1);
  s.metallic = u_Terrain.layer0Metallic * mix(1.0, texture(layer0MetallicTexture, uv).b, hasM);
  return s;
}

LayerSample sampleLayer1(vec2 uv) {
  LayerSample s;
  float hasA = float((u_Terrain.textureMask >> 4) & 1);
  vec4 aTex = mix(vec4(1.0), texture(layer1AlbedoTexture, uv), hasA);
  s.albedo = vec4(u_Terrain.layer1Albedo, 1.0) * aTex;

  float hasN = float((u_Terrain.textureMask >> 5) & 1);
  s.normalTS = mix(vec3(0.0, 0.0, 1.0), texture(layer1NormalTexture, uv).rgb * 2.0 - 1.0, hasN);

  float hasR = float((u_Terrain.textureMask >> 6) & 1);
  s.roughness = u_Terrain.layer1Roughness * mix(1.0, texture(layer1RoughnessTexture, uv).r, hasR);

  float hasM = float((u_Terrain.textureMask >> 7) & 1);
  s.metallic = u_Terrain.layer1Metallic * mix(1.0, texture(layer1MetallicTexture, uv).b, hasM);
  return s;
}

LayerSample sampleLayer2(vec2 uv) {
  LayerSample s;
  float hasA = float((u_Terrain.textureMask >> 8) & 1);
  vec4 aTex = mix(vec4(1.0), texture(layer2AlbedoTexture, uv), hasA);
  s.albedo = vec4(u_Terrain.layer2Albedo, 1.0) * aTex;

  float hasN = float((u_Terrain.textureMask >> 9) & 1);
  s.normalTS = mix(vec3(0.0, 0.0, 1.0), texture(layer2NormalTexture, uv).rgb * 2.0 - 1.0, hasN);

  float hasR = float((u_Terrain.textureMask >> 10) & 1);
  s.roughness = u_Terrain.layer2Roughness * mix(1.0, texture(layer2RoughnessTexture, uv).r, hasR);

  float hasM = float((u_Terrain.textureMask >> 11) & 1);
  s.metallic = u_Terrain.layer2Metallic * mix(1.0, texture(layer2MetallicTexture, uv).b, hasM);
  return s;
}

// Cheap pseudo-noise: sum of three rotated sines, output ~ [-1, 1]
float shoulderSines(vec2 p) {
  float n = 0.0;
  n += sin(p.x * 1.31 + p.y * 0.71) * 0.5;
  n += sin(p.x * 0.27 - p.y * 1.83) * 0.3;
  n += sin(p.x * 2.13 + p.y * 1.97) * 0.2;
  return n;
}

vec2 shoulderDomainWarp(vec2 p) {
  if (u_Terrain.shoulderWarpAmplitude <= 0.0) return p;
  vec2 q = p * u_Terrain.shoulderWarpScale;
  vec2 offset = vec2(shoulderSines(q), shoulderSines(q + vec2(17.3, 31.7)));
  return p + offset * u_Terrain.shoulderWarpAmplitude;
}

float distanceToPolyline2D(vec2 p) {
  int n = u_Terrain.roadSegmentCount;
  if (n < 2) return 1e9;
  float minDist = 1e9;
  for (int i = 0; i < n - 1; i++) {
    vec2 a = u_Terrain.roadSegments[i].xy;
    vec2 b = u_Terrain.roadSegments[i + 1].xy;
    vec2 ab = b - a;
    float t = clamp(dot(p - a, ab) / max(dot(ab, ab), 1e-8), 0.0, 1.0);
    vec2 closest = a + t * ab;
    minDist = min(minDist, distance(p, closest));
  }
  return minDist;
}
