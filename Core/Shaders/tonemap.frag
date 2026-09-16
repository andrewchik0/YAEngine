layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

#include "common.glsl"
#include "octahedron.glsl"
#include "debug_ramps.glsl"
#include "noise.glsl"

layout(set = 1, binding = 0) uniform sampler2D frame;
layout(set = 1, binding = 1) uniform sampler2D aoTexture;
layout(set = 1, binding = 2) uniform sampler2D gbuffer0Texture;
layout(set = 1, binding = 3) uniform sampler2D gbuffer1Texture;
layout(set = 1, binding = 4) uniform sampler2D velocityTexture;
layout(set = 1, binding = 5) uniform sampler2D preResolveTexture;
// SSGI diagnostics: the denoised screen irradiance + fallback weight, and the
// reprojected radiance whose alpha is the reprojection validity.
layout(set = 1, binding = 6) uniform sampler2D ssgiTexture;
layout(set = 1, binding = 7) uniform sampler2D ssgiRadianceTexture;
// The path tracer's two outputs: this frame's single sample, and the running mean it keeps
// while nothing moves. Both are HDR scene radiance.
layout(set = 1, binding = 8) uniform sampler2D pathTraceNoisyTexture;
layout(set = 1, binding = 9) uniform sampler2D pathTraceAccumTexture;
// Ray reconstruction guides, written by pt_guides.comp while the path tracing render path
// is effective. Only the combined PT Guides view reads them.
layout(set = 1, binding = 10) uniform sampler2D ptDiffuseAlbedoTexture;
layout(set = 1, binding = 11) uniform sampler2D ptSpecularAlbedoTexture;
layout(set = 1, binding = 12) uniform sampler2D ptNormalRoughnessTexture;
// The tracer's specular motion vectors, read only by the PT Specular Motion view.
layout(set = 1, binding = 13) uniform sampler2D ptSpecularMotionTexture;

layout(std430, set = 2, binding = 0) readonly buffer ExposureSSBO
{
  float autoExposure;
};

layout(set = 3, binding = 0) uniform sampler2D bloomTexture;

#include "tonemap.glsl"

// Uniform noise reshaped into a triangular distribution over [-1, 1]. Only that shape makes
// the quantization error white and independent of the value being quantized; a flat uniform
// dither leaves the error correlated with the signal, which is the banding it is meant to hide.
float triangularDither(vec2 fragCoord)
{
  float u = interleavedGradientNoise(fragCoord) * 2.0 - 1.0;
  return sign(u) * (1.0 - sqrt(max(1.0 - abs(u), 0.0)));
}

void main()
{
  // Debug views - raw G-buffer, no tone mapping
  switch (u_Frame.currentTexture)
  {
  case 1: // Albedo (linear g-buffer value, needs gamma encoding like the final image)
    outColor = vec4(pow(texture(gbuffer0Texture, uv).rgb, vec3(1.0 / u_Frame.gamma)), 1.0);
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
  case 5: // AO
    {
      float ao = texture(aoTexture, uv).r;
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
  case DEBUG_VIEW_SSGI_VALIDITY: // white = history reprojects cleanly, black = disocclusion
    {
      float validity = texture(ssgiRadianceTexture, uv).a;
      outColor = vec4(vec3(validity), 1.0);
    }
    return;
  case DEBUG_VIEW_SSGI_SCREEN: // screen-gathered irradiance alone, HDR so tone-mapped
    {
      vec3 color = texture(ssgiTexture, uv).rgb;
      float ssgiExposure = autoExposure * u_Frame.exposure;
      color = applyTonemap(color * ssgiExposure);
      color = pow(color, vec3(1.0 / u_Frame.gamma));
      outColor = vec4(color, 1.0);
    }
    return;
  case DEBUG_VIEW_SSGI_FALLBACK: // white = all volume fallback, black = all screen
    outColor = vec4(debugFallbackHeat(texture(ssgiTexture, uv).a), 1.0);
    return;
  case DEBUG_VIEW_PT_NOISY:     // one path traced sample per pixel, this frame's
  case DEBUG_VIEW_PT_REFERENCE: // the running mean of every sample since the last reset
    {
      // Scene radiance in the same linear units the deferred pass produces, so it goes
      // through the same operator the final image does - exposure, tone map, gamma. Bloom
      // is left out: it is composited from the raster chain and would be this image plus a
      // glow of a different one. Auto-exposure still meters the rasterized frame, which is
      // what makes the two comparable at a glance rather than each finding its own key.
      vec3 color = u_Frame.currentTexture == DEBUG_VIEW_PT_NOISY
        ? texture(pathTraceNoisyTexture, uv).rgb
        : texture(pathTraceAccumTexture, uv).rgb;
      color = color * (autoExposure * u_Frame.exposure);
      color = applyTonemap(color);
      outColor = vec4(pow(color, vec3(1.0 / u_Frame.gamma)), 1.0);
    }
    return;
  case DEBUG_VIEW_PT_MAX_CONTRIB:
  case DEBUG_VIEW_PT_NEE:
  case DEBUG_VIEW_PT_ENVIRONMENT:
  case DEBUG_VIEW_PT_DELTA_LIGHTS:
    // The tracer stored a base-10 logarithm rather than radiance, so it is raised back before
    // the shared ramp takes it - and no exposure and no tone map, which would compress exactly
    // the range these views exist to show.
    outColor = vec4(debugLogMagnitude(pow(10.0, texture(pathTraceNoisyTexture, uv).r), uv), 1.0);
    return;
  case DEBUG_VIEW_PT_NONFINITE:
    {
      // The tracer stored a PT_NF_* code in R and the bounce in G as raw integers - the
      // numbers are meant to be read out of the captured image, and the screen only has to
      // say where a hit is and roughly which code it was. Black is a path that stayed
      // finite, which is every pixel of a healthy frame.
      float code = texture(pathTraceNoisyTexture, uv).r;
      outColor = vec4(code <= 0.0
        ? vec3(0.0)
        : debugCategoryHue(code / float(PT_NF_COUNT)), 1.0);
    }
    return;
  case DEBUG_VIEW_HDR_MAGNITUDE:
    // The resolved image the tone map is about to consume, on the same scale. `frame` is
    // whichever buffer the active render path resolved into, so this reads the rasterized and
    // the path traced frame identically and the two can be compared number for number.
    {
      vec3 hdr = texture(frame, uv).rgb;
      outColor = vec4(debugLogMagnitude(max(hdr.r, max(hdr.g, hdr.b)), uv), 1.0);
    }
    return;
  case DEBUG_VIEW_PT_GUIDES:
    {
      // All three guide buffers at once, one per quadrant of the same frame: the two
      // demodulation albedos on top, the world normal and the roughness underneath.
      // Render only lets this view through while the path tracing render path is
      // effective, which is the only time the pass that fills them runs.
      vec2 tileUV = fract(uv * 2.0);
      bool right = uv.x >= 0.5;
      bool bottom = uv.y >= 0.5;

      vec3 value;
      if (!bottom && !right)
        value = pow(texture(ptDiffuseAlbedoTexture, tileUV).rgb, vec3(1.0 / u_Frame.gamma));
      else if (!bottom)
        value = pow(texture(ptSpecularAlbedoTexture, tileUV).rgb, vec3(1.0 / u_Frame.gamma));
      else if (!right)
        value = texture(ptNormalRoughnessTexture, tileUV).rgb * 0.5 + 0.5;
      else
        value = vec3(texture(ptNormalRoughnessTexture, tileUV).a);

      outColor = vec4(value, 1.0);
    }
    return;
  case DEBUG_VIEW_PT_SPECULAR_MOTION: // on the Velocity view's scale, so the two compare directly
    {
      vec2 specularMotion = texture(ptSpecularMotionTexture, uv).rg;
      outColor = vec4(abs(specularMotion) * 200.0, 0.0, 1.0);
    }
    return;
  case DEBUG_VIEW_AMBIENT_ONLY:     // raw linear ambient term, no direct light
  case DEBUG_VIEW_AMBIENT_DIFFUSE:  // diffuse half of it - irradiance volumes
  case DEBUG_VIEW_AMBIENT_SPECULAR: // specular half of it - reflection probes
  case DEBUG_VIEW_DIRECT_ONLY:      // direct light alone, comparable with the three above
    // deferred_lighting.frag already wrote the final value, and Render forces SSR
    // and TAA off for these views, so `frame` still holds it unmodified. Still
    // linear light though, so it needs the same gamma encoding as the final image.
    outColor = vec4(pow(texture(frame, uv).rgb, vec3(1.0 / u_Frame.gamma)), 1.0);
    return;
  case DEBUG_VIEW_PROBE_INDEX:      // one color per dominant probe, black = no local probe
  case DEBUG_VIEW_PROBE_FALLBACK:   // heat ramp of the skybox fallback share
  case DEBUG_VIEW_VOLUME_COVERAGE:  // one color per irradiance volume, black = skybox
  case DEBUG_VIEW_VOLUME_LEVEL:     // one color per brick spacing level, black = skybox
    // These are synthetic display-space colors, not linear light - no gamma encoding.
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

  // The attachment is sRGB, so the hardware encodes this value before quantizing it: a flat
  // 1/255 of dither here would be an enormous step in the shadows and invisible in the
  // highlights. The amplitude is one encoded 8-bit step converted back into linear units,
  // d(linear)/d(encoded) of the sRGB curve. Below linear 0.003 the curve is a straight line
  // and this over-darkens the step slightly, which only costs noise where the encoded buffer
  // already has codes to spare.
  if (u_Frame.ditherEnabled != 0)
  {
    vec3 quantizationStep = 2.2749 * pow(max(color, 1e-5), vec3(0.58333)) / 255.0;
    color += quantizationStep * triangularDither(gl_FragCoord.xy);
  }

  outColor = vec4(color, 1.0);
}
