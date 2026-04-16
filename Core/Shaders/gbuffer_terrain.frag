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

  // Slope-based blend factor: 0 = flat (ground), 1 = steep (rock)
  float slope = 1.0 - abs(dot(normalize(inNormal), vec3(0.0, 1.0, 0.0)));
  float slopeMask = smoothstep(u_Terrain.slopeStart, u_Terrain.slopeEnd, slope);

  // Shoulder mask: 1 = inside inner radius (gravel), 0 = outside outer radius (sand)
  float shoulderMask = 0.0;
  if (u_Terrain.shoulderOuterRadius > 0.0) {
    vec2 sampleP = shoulderDomainWarp(inPosition.xz);
    float distToRoad = distanceToPolyline2D(sampleP);
    shoulderMask = 1.0 - smoothstep(u_Terrain.shoulderInnerRadius, u_Terrain.shoulderOuterRadius, distToRoad);
  }

  vec2 uv0 = inTexCoord;
  vec2 uv1 = inTexCoord * u_Terrain.layer1UvScale / u_Terrain.layer0UvScale;
  vec2 uv2 = inTexCoord * u_Terrain.layer2UvScale / u_Terrain.layer0UvScale;

  LayerSample sand   = sampleLayer0(uv0);
  LayerSample rock   = sampleLayer1(uv1);
  LayerSample gravel = sampleLayer2(uv2);

  // Ground = blend sand <-> gravel by shoulder distance, then ground <-> rock by slope.
  vec4 albedoGround    = mix(sand.albedo,   gravel.albedo,   shoulderMask);
  vec3 normalTSGround  = mix(sand.normalTS, gravel.normalTS, shoulderMask);
  float roughnessGround = mix(sand.roughness, gravel.roughness, shoulderMask);
  float metallicGround  = mix(sand.metallic,  gravel.metallic,  shoulderMask);

  vec4 albedo    = mix(albedoGround,    rock.albedo,   slopeMask);
  vec3 normalTS  = mix(normalTSGround,  rock.normalTS, slopeMask);
  float roughness = mix(roughnessGround, rock.roughness, slopeMask);
  float metallic  = mix(metallicGround,  rock.metallic,  slopeMask);

  albedo = vec4(pow(albedo.rgb, vec3(gamma)), albedo.a);

  // Bit positions: layer0 normal=1, layer1 normal=5, layer2 normal=9
  float hasAnyNormal = float(((u_Terrain.textureMask >> 1) | (u_Terrain.textureMask >> 5) | (u_Terrain.textureMask >> 9)) & 1);
  vec3 normal = mix(inNormal, normalize(inTBN * normalize(normalTS)), hasAnyNormal);

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
