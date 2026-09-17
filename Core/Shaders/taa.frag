layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

#include "common.glsl"

layout(set = 1, binding = 0) uniform sampler2D frame;
layout(set = 1, binding = 1) uniform sampler2D history;
layout(set = 1, binding = 2) uniform sampler2D velocityTexture;
layout(set = 1, binding = 3) uniform sampler2D depthTexture;

#include "variance_clipping.glsl"
#include "reprojection.glsl"

const float BACKGROUND_DEPTH = 0.0;

// Reversed Z, so the nearest sample of the neighbourhood is the one with the largest depth.
// Taking the velocity from there instead of from the pixel itself lets a silhouette pull its
// own motion over the pixels around it; without it the pixels along a moving edge reproject
// into the background behind it and thin geometry drags a trail.
ivec2 closestDepthOffset(ivec2 coord, ivec2 maxCoord, out float closestDepth)
{
  ivec2 best = ivec2(0);
  closestDepth = texelFetch(depthTexture, coord, 0).r;

  for (int y = -1; y <= 1; ++y)
  {
    for (int x = -1; x <= 1; ++x)
    {
      float d = texelFetch(depthTexture, clamp(coord + ivec2(x, y), ivec2(0), maxCoord), 0).r;
      if (d > closestDepth)
      {
        closestDepth = d;
        best = ivec2(x, y);
      }
    }
  }

  return best;
}

// Five tap Catmull-Rom. The history is resampled every frame, so a bilinear fetch of an
// already bilinearly fetched image loses a little high frequency per frame and a panning
// camera smears; the negative lobes of a cubic put it back. Standing still the reprojection
// lands on a texel centre, where this collapses to that one texel.
vec3 sampleHistoryCatmullRom(vec2 sampleUV)
{
  vec2 texSize = vec2(textureSize(history, 0));
  vec2 samplePos = sampleUV * texSize;
  vec2 texPos1 = floor(samplePos - 0.5) + 0.5;
  vec2 f = samplePos - texPos1;

  vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
  vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
  vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
  vec2 w3 = f * f * (-0.5 + 0.5 * f);

  // w1 and w2 are carried by one bilinear tap placed between their texels, which is what turns
  // a 4x4 kernel into five fetches. w12 is 1 + 0.5f - 0.5f*f over the unit interval, so it
  // never approaches zero.
  vec2 w12 = w1 + w2;
  vec2 offset12 = w2 / w12;

  vec2 texPos0 = (texPos1 - 1.0) / texSize;
  vec2 texPos3 = (texPos1 + 2.0) / texSize;
  vec2 texPos12 = (texPos1 + offset12) / texSize;

  vec3 result = vec3(0.0);
  float weightSum = 0.0;

  result += texture(history, vec2(texPos12.x, texPos0.y)).rgb * (w12.x * w0.y);
  weightSum += w12.x * w0.y;
  result += texture(history, vec2(texPos0.x, texPos12.y)).rgb * (w0.x * w12.y);
  weightSum += w0.x * w12.y;
  result += texture(history, vec2(texPos12.x, texPos12.y)).rgb * (w12.x * w12.y);
  weightSum += w12.x * w12.y;
  result += texture(history, vec2(texPos3.x, texPos12.y)).rgb * (w3.x * w12.y);
  weightSum += w3.x * w12.y;
  result += texture(history, vec2(texPos12.x, texPos3.y)).rgb * (w12.x * w3.y);
  weightSum += w12.x * w3.y;

  // Dropping the four corner taps takes up to 1.6% of the weight with it, which would show as
  // a brightness drift that only appears while the camera pans. A cubic also overshoots into
  // negative values on a high contrast edge, and the YCoCg conversion below has no meaning
  // there.
  return max(result / weightSum, vec3(0.0));
}

void main()
{
  // Passthrough when TAA is disabled
  if (u_Frame.taaEnabled == 0)
  {
    outColor = vec4(texture(frame, uv).rgb, 1.0);
    return;
  }

  ivec2 screenSpaceUV = ivec2(gl_FragCoord.xy);
  ivec2 maxCoord = textureSize(frame, 0) - ivec2(1);

  // Reconstruction filter. Every pixel of this frame was rasterized at the same sub-pixel
  // jitter offset, so a raw point sample carries that offset into the blend and the output
  // wobbles by (1 - blendFactor) of the sample's own frame-to-frame swing. Weighting the
  // neighbourhood by each sample's distance to the pixel centre re-centres the estimate.
  vec2 jitterPixels = vec2(u_Frame.jitterX, u_Frame.jitterY)
                    * 0.5 * vec2(u_Frame.screenWidth, u_Frame.screenHeight);

  // exp(-a * (dx*dx + dy*dy)) factors into exp(-a*dx*dx) * exp(-a*dy*dy), so the nine weights
  // come from six exponentials instead of nine. They only depend on uniforms.
  float wx[3];
  float wy[3];
  for (int i = -1; i <= 1; ++i)
  {
    float dx = float(i) + jitterPixels.x;
    float dy = float(i) + jitterPixels.y;
    wx[i + 1] = exp(RECONSTRUCTION_FILTER_FALLOFF * dx * dx);
    wy[i + 1] = exp(RECONSTRUCTION_FILTER_FALLOFF * dy * dy);
  }

  vec3 filtered = vec3(0.0);
  float weightSum = 0.0;

  for (int y = -1; y <= 1; ++y)
  {
    for (int x = -1; x <= 1; ++x)
    {
      float w = wx[x + 1] * wy[y + 1];
      filtered += texelFetch(frame, clamp(screenSpaceUV + ivec2(x, y), ivec2(0), maxCoord), 0).rgb * w;
      weightSum += w;
    }
  }

  vec3 currentColor = filtered / max(weightSum, 1e-5);
  vec3 currentYCoCg = rgbToYCoCg(currentColor);

  float closestDepth;
  ivec2 velocityOffset = closestDepthOffset(screenSpaceUV, maxCoord, closestDepth);

  // Nothing in the neighbourhood was rasterized, so the velocity buffer still holds its clear
  // value everywhere around here and the view ray has to be reprojected instead.
  vec2 velocity = closestDepth <= BACKGROUND_DEPTH
    ? backgroundVelocity(uv * 2.0 - 1.0)
    : texelFetch(velocityTexture, clamp(screenSpaceUV + velocityOffset, ivec2(0), maxCoord), 0).rg;

  float speed = length(velocity * vec2(u_Frame.screenWidth, u_Frame.screenHeight));

  float clampSigma = u_Frame.taaClampSigma
    * mix(CLAMP_WIDENING_AT_REST, 1.0, clamp(speed / CLAMP_WIDENING_FADE_SPEED, 0.0, 1.0));

  vec3 colorMin = vec3(0);
  vec3 colorMax = vec3(0);
  getVarianceClippingBounds(currentYCoCg, frame, screenSpaceUV, clampSigma, colorMin, colorMax);

  vec2 historyUV = uv - velocity;

  bool historyValid = historyUV.x >= 0.0 && historyUV.x <= 1.0
                   && historyUV.y >= 0.0 && historyUV.y <= 1.0;

  vec3 result;
  if (historyValid)
  {
    vec3 historyCol = tonemapYCoCg(rgbToYCoCg(sampleHistoryCatmullRom(historyUV)));
    historyCol = clamp(historyCol, colorMin, colorMax);

    float blendFactor = mix(EMA_IIR_INVERSE_CUTOFF_FREQUENCY, 0.5, clamp(speed / 16.0, 0.0, 1.0));

    // The blend itself happens where the tone map has already compressed the range. That is
    // what stops a bright sub-pixel highlight from dominating the average it flickers in and
    // out of: at Y = 10 a sample weighs about a tenth of what it weighs in linear light. It
    // costs a little energy on pixels that really do alternate, which is the sparkle.
    result = mix(tonemapYCoCg(currentYCoCg), historyCol, blendFactor);
    result = yCoCgToRGB(inverseTonemapYCoCg(result));
  }
  else
  {
    result = currentColor;
  }

  // YCoCg has corners outside the RGB cube, and a clipped history can land in one of them.
  outColor = vec4(max(result, vec3(0.0)), 1.0);
}
