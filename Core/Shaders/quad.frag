#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

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

layout(set = 1, binding = 0) uniform sampler2D frame;

#include "post.glsl"

void main()
{
  vec3 color = texture(frame, uv).rgb;

  color = color * u_Data.exposure;
  color = ACESFilm(color);
  color = pow(color, vec3(1.0 / u_Data.gamma));

  outColor = vec4(color, 1.0);
}
