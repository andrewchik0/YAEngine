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
layout(set = 1, binding = 8) uniform samplerCube cubemapTexture;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;

#include "post.glsl"

float Hash(vec2 p)
{
  return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}


void main() {
  float base = float(u_Material.textureMask & 1);
  vec4 albedo = texture(baseColorTexture, inTexCoord) * base + vec4(u_Material.albedo, 1.0) * (1 - base);

  if (albedo.a < 0.5)
  {
    discard;
  }
  else
  {
    outColor = vec4(albedo.rgb, 1.0);
    outNormal = vec4(inNormal, 1.0);
  }
}
