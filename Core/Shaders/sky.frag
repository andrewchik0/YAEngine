#version 450

layout(set = 0, binding = 0) uniform samplerCube skyboxSampler;

layout(location = 0) in vec3 inCoord;
layout(location = 0) out vec4 fragColor;

#include "post.glsl"

void main()
{
  vec3 hdrColor = texture(skyboxSampler, normalize(inCoord)).rgb;

  float exposure = 0.1;
  vec3 mapped = hdrColor * exposure;

  mapped = ACESFilm(mapped);
  vec3 finalColor = pow(mapped, vec3(1.0/2.2));
  fragColor = vec4(finalColor, 1.0);
}
