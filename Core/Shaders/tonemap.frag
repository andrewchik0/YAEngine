layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

#include "common.glsl"
#include "octahedron.glsl"

layout(set = 1, binding = 0) uniform sampler2D frame;
layout(set = 1, binding = 1) uniform sampler2D ssaoTexture;
layout(set = 1, binding = 2) uniform sampler2D gbuffer0Texture;
layout(set = 1, binding = 3) uniform sampler2D gbuffer1Texture;
layout(set = 1, binding = 4) uniform sampler2D velocityTexture;
layout(set = 1, binding = 5) uniform sampler2D preResolveTexture;

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
  case 7: // Wireframe (gbuffer0 albedo already holds wireframe color on black clear)
    outColor = vec4(texture(gbuffer0Texture, uv).rgb, 1.0);
    return;
  case 8: // TAA delta: how far the resolve moved the frame away from its own input.
          // Near-black everywhere means the history is not contributing and TAA is a no-op.
    {
      vec3 preResolve = texture(preResolveTexture, uv).rgb;
      vec3 resolved = texture(frame, uv).rgb;
      outColor = vec4(abs(resolved - preResolve) * 10.0, 1.0);
    }
    return;
  case 9: // Motion vectors, scaled to be visible. Must be pure black on a static camera:
          // red = horizontal, green = vertical, brightness = magnitude.
    {
      vec2 velocity = texture(velocityTexture, uv).rg;
      outColor = vec4(abs(velocity) * 200.0, 0.0, 1.0);
    }
    return;
  case DEBUG_VIEW_AMBIENT_ONLY:     // raw linear ambient term, no direct light
  case DEBUG_VIEW_AMBIENT_DIFFUSE:  // diffuse half of it - irradiance volumes
  case DEBUG_VIEW_AMBIENT_SPECULAR: // specular half of it - reflection probes
  case DEBUG_VIEW_PROBE_INDEX:      // one color per dominant probe, black = no local probe
  case DEBUG_VIEW_PROBE_FALLBACK:   // heat ramp of the skybox fallback share
  case DEBUG_VIEW_VOLUME_COVERAGE:  // one color per irradiance volume, black = skybox
    // deferred_lighting.frag already wrote the final value, and Render forces SSR
    // and TAA off for these views, so `frame` still holds it unmodified.
    outColor = vec4(texture(frame, uv).rgb, 1.0);
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

  outColor = vec4(color, 1.0);
}
