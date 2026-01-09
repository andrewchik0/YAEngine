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
} u_Data;

layout(set = 1, binding = 0) uniform PerMaterialUBO {
  vec3 albedo;
  float roughness;
  vec3 emissivity;
  float specular;
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

#include "post.glsl"

float Hash(vec2 p)
{
  return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}


void main() {
  float hasNormal = float((u_Material.textureMask >> 5) & 1);
  vec3 n_ts = texture(normalTexture, inTexCoord).rgb * 2.0 - 1.0;
  vec3 normal = normalize(inTBN * n_ts) * hasNormal + (1.0 - hasNormal) * inNormal;

  float base = float(u_Material.textureMask & 1);
  vec4 col = texture(baseColorTexture, inTexCoord) * base + vec4(u_Material.albedo, 1.0) * (1 - base);
  outColor = vec4(col.xyz * max(dot(normalize(vec3(0.5, 1, 0.5)), normal), 0.1), 1.0);

  vec3 viewDir = normalize(u_Data.cameraPosition - inPosition);
  vec3 reflectDir = reflect(-viewDir, normalize(normal));

  vec3 hdrColor = textureLod(cubemapTexture, reflectDir, 4).rgb;

  float exposure = 0.6;
  vec3 mapped = hdrColor * exposure;

  mapped = ACESFilm(mapped);
  vec3 finalColor = pow(mapped, vec3(1.0/2.2));

  float r = Hash(vec2(length(u_Data.cameraPosition), u_Data.cameraDirection.x + u_Data.cameraDirection.z) + inTexCoord + vec2(u_Data.time));

  if (r > col.a)
  {
    discard;
  }
  else
  {
    outColor *= vec4(finalColor, 1.0);
    outColor *= col.a;
  }
}
