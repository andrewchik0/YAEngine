#version 450

layout(location = 0) in vec2 texCoord;
layout(location = 1) in vec3 normal;

layout(set = 0, binding = 0) uniform PerFrameUBO {
  mat4 view;
  mat4 proj;
  float time;
} ubo;

layout(set = 1, binding = 0) uniform PerMaterialUBO {
  vec3 albedo;
  float roughness;
  vec3 emissivity;
  float specular;
  int textureMask;
  int sg;
} material;
layout(set = 1, binding = 1) uniform sampler2D baseColorTexture;
layout(set = 1, binding = 2) uniform sampler2D metallicTexture;
layout(set = 1, binding = 3) uniform sampler2D roughnessTexture;
layout(set = 1, binding = 4) uniform sampler2D specularTexture;
layout(set = 1, binding = 5) uniform sampler2D emissiveTexture;
layout(set = 1, binding = 6) uniform sampler2D normalTexture;
layout(set = 1, binding = 7) uniform sampler2D heightTexture;

layout(location = 0) out vec4 outColor;

void main() {
  float base = float(material.textureMask & 1);
  vec3 col = texture(baseColorTexture, texCoord).xyz * base + material.albedo * (1 - base);
  outColor = vec4(col * max(dot(normalize(vec3(0.5, 1, 0.5)), normal), 0.1), 1.0);
}
