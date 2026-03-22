#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outPosition;
layout(location = 3) out mat3 outTBN;
layout(location = 6) out vec4 outCurClipPos;
layout(location = 7) out vec4 outPrevClipPos;

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
} u_Data;

layout(push_constant) uniform PushConstants
{
  mat4 world;
  int offset;
} pc;

void main() {
  vec4 worldPos = pc.world * vec4(inPosition, 1.0);
  gl_Position = u_Data.proj * u_Data.view * worldPos;
  outCurClipPos = gl_Position;
  outPrevClipPos = u_Data.prevProj * u_Data.prevView * worldPos;
  outTexCoord = inTexCoord;
  outPosition = vec3(worldPos);
  outNormal = normalize(mat3(pc.world) * inNormal);

  mat3 normalMatrix = transpose(inverse(mat3(pc.world)));

  vec3 N = normalize(normalMatrix * inNormal);
  vec3 T = normalize(normalMatrix * inTangent.xyz);

  T = normalize(T - N * dot(T, N));
  vec3 B = cross(N, T) * inTangent.w;

  outTBN = mat3(T, B, N);
}
