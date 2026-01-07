#version 450

layout(set = 0, binding = 0) uniform sampler2D uEquirectMap;

layout(location = 0) in vec3 vDir;
layout(location = 0) out vec4 outColor;

const vec2 invAtan = vec2(
    0.15915494309189535,
    0.3183098861837907
);

vec2 SampleSphericalMap(vec3 v)
{
  vec2 uv;
  uv.x = atan(v.z, v.x);
  uv.y = asin(clamp(v.y, -1.0, 1.0));
  uv *= invAtan;
  uv += 0.5;
  return uv;
}

void main()
{
  vec3 dir = normalize(vDir);
  vec2 uv = SampleSphericalMap(dir);
  vec3 color = texture(uEquirectMap, uv).rgb;
  outColor = vec4(color, 1.0);
}
