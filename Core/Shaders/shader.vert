#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

layout(location = 0) out vec2 texCoord;
layout(location = 1) out vec3 normal;

layout(set = 0, binding = 0) uniform PerFrameUBO {
  mat4 view;
  mat4 proj;
  float time;
} ubo;

void main() {
  gl_Position = ubo.proj * ubo.view * vec4(inPosition, 1.0);
  texCoord = inTexCoord;
  normal = inNormal;
}
