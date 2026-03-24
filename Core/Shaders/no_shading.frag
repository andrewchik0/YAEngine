#version 450

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inPosition;
layout(location = 3) in mat3 inTBN;
layout(location = 6) in vec4 inCurClipPos;
layout(location = 7) in vec4 inPrevClipPos;

layout(set = 0, binding = 0) uniform PerFrameUBO {
  mat4 view;
  mat4 proj;
  mat4 invProj;
  mat4 prevView;
  mat4 prevProj;
  vec3 cameraPosition;
  float time;
  vec3 cameraDirection;
  float gamma;
  float exposure;
  int currentTexture;
  float near;
  float far;
  float fov;
  int screenWidth;
  int screenHeight;
  int ssaoEnabled;
  int ssrEnabled;
  int taaEnabled;
  float jitterX;
  float jitterY;
  int hizMipCount;
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

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec2 outMaterial;
layout(location = 3) out vec4 outAlbedo;
layout(location = 4) out vec2 outVelocity;

void main() {
  float base = float(u_Material.textureMask & 1);
  vec4 albedo = texture(baseColorTexture, inTexCoord) * base + vec4(u_Material.albedo, 1.0) * (1.0 - base);

  vec2 curNDC = inCurClipPos.xy / inCurClipPos.w;
  vec2 prevNDC = inPrevClipPos.xy / inPrevClipPos.w;
  vec2 velocity = (curNDC - prevNDC) * 0.5;

  if (albedo.a < 0.5)
  {
    discard;
  }
  else
  {
    outColor = vec4(albedo.rgb, 1.0);
    outNormal = vec4(inNormal, 1.0);
    outMaterial = vec2(1.0, 0.0);
    outAlbedo = vec4(albedo.rgb, 1.0);
    outVelocity = velocity;
  }
}
