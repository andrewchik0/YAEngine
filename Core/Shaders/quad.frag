layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

#include "common.glsl"

layout(set = 1, binding = 0) uniform sampler2D frame;
layout(set = 1, binding = 1) uniform sampler2D ssaoTexture;
layout(set = 1, binding = 2) uniform sampler2D albedoTexture;
layout(set = 1, binding = 3) uniform sampler2D normalTexture;
layout(set = 1, binding = 4) uniform sampler2D materialTexture;

#include "post.glsl"

void main()
{
  // Debug views — raw G-buffer, no tone mapping
  switch (u_Frame.currentTexture)
  {
  case 1: // Albedo
    outColor = vec4(texture(albedoTexture, uv).rgb, 1.0);
    return;
  case 2: // Metallic
    outColor = vec4(vec3(texture(materialTexture, uv).g), 1.0);
    return;
  case 3: // Roughness
    outColor = vec4(vec3(texture(materialTexture, uv).r), 1.0);
    return;
  case 4: // Normals
    outColor = vec4(texture(normalTexture, uv).rgb * 0.5 + 0.5, 1.0);
    return;
  case 5: // SSAO
    {
      float ao = texture(ssaoTexture, uv).r;
      outColor = vec4(ao, ao, ao, 1.0);
    }
    return;
  case 6: // SSR only (base zeroed in ssr.frag, HDR needs tone mapping)
    {
      vec3 color = texture(frame, uv).rgb;
      color = color * u_Frame.exposure;
      color = acesFilm(color);
      color = pow(color, vec3(1.0 / u_Frame.gamma));
      outColor = vec4(color, 1.0);
    }
    return;
  }

  // Default: tone-mapped final image
  vec3 color = texture(frame, uv).rgb;

  color = color * u_Frame.exposure;
  color = acesFilm(color);
  color = pow(color, vec3(1.0 / u_Frame.gamma));

  outColor = vec4(color, 1.0);
}
