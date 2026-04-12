layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

#include "common.glsl"
#include "octahedron.glsl"

layout(set = 1, binding = 0) uniform sampler2D frame;
layout(set = 1, binding = 1) uniform sampler2D ssaoTexture;
layout(set = 1, binding = 2) uniform sampler2D gbuffer0Texture;
layout(set = 1, binding = 3) uniform sampler2D gbuffer1Texture;

layout(std430, set = 2, binding = 0) readonly buffer ExposureSSBO
{
  float autoExposure;
};

layout(set = 3, binding = 0) uniform sampler2D bloomTexture;

#include "tonemap.glsl"

void main()
{
  // Debug views - raw G-buffer, no tone mapping
  switch (u_Frame.currentTexture)
  {
  case 1: // Albedo
    outColor = vec4(texture(gbuffer0Texture, uv).rgb, 1.0);
    return;
  case 2: // Metallic (from GBuffer0.a)
    outColor = vec4(vec3(texture(gbuffer0Texture, uv).a), 1.0);
    return;
  case 3: // Roughness (from GBuffer1.b)
    outColor = vec4(vec3(texture(gbuffer1Texture, uv).b), 1.0);
    return;
  case 4: // Normals (octahedron decode from GBuffer1.rg)
    {
      vec2 enc = texture(gbuffer1Texture, uv).rg;
      vec3 normal = octDecode(enc * 2.0 - 1.0);
      outColor = vec4(normal * 0.5 + 0.5, 1.0);
    }
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
      float ssrExposure = autoExposure * u_Frame.exposure;
      color = color * ssrExposure;
      color = applyTonemap(color);
      color = pow(color, vec3(1.0 / u_Frame.gamma));
      outColor = vec4(color, 1.0);
    }
    return;
  }

  // Default: tone-mapped final image
  vec3 color = texture(frame, uv).rgb;

  // Add bloom before tone mapping
  color += u_Frame.bloomIntensity * texture(bloomTexture, uv).rgb;

  float finalExposure = autoExposure * u_Frame.exposure;
  color = color * finalExposure;
  color = applyTonemap(color);
  color = pow(color, vec3(1.0 / u_Frame.gamma));

  if (u_Frame.ssaoEnabled != 0)
  {
    float ao = texture(ssaoTexture, uv).r;
    color *= ao;
  }

  outColor = vec4(color, 1.0);
}
