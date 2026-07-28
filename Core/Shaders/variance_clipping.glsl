//
// https://www.shadertoy.com/view/tt3fRX#
//
// ------------------------------------------------------------------------- //

// CONFIG

#define EMA_IIR_INVERSE_CUTOFF_FREQUENCY        (0.97)      // 0.0 - 0.999
// Blackman-Harris approximation used to resolve the jittered sample back onto the pixel
// centre. More negative = tighter filter, sharper image, less flicker suppression.
#define RECONSTRUCTION_FILTER_FALLOFF           (-2.29)
// Box width lives in u_Frame.taaClampSigma - runtime tunable per scene.

// ------------------------------------------------------------------------- //

// https://www.shadertoy.com/view/4dSBDt

vec3 rgbToYCoCg(vec3 RGB) {
  float cTerm = 0.5 * 256.0 / 255.0;
  float Y  = dot(RGB, vec3( 1, 2,  1)) * 0.25;
  float Co = dot(RGB, vec3( 2, 0, -2)) * 0.25 + cTerm;
  float Cg = dot(RGB, vec3(-1, 2, -1)) * 0.25 + cTerm;
  return vec3(Y, Co, Cg);
}

vec3 yCoCgToRGB(vec3 YCoCg) {
  float cTerm = 0.5 * 256.0 / 255.0;
  float Y  = YCoCg.x;
  float Co = YCoCg.y - cTerm;
  float Cg = YCoCg.z - cTerm;
  float R  = Y + Co - Cg;
  float G  = Y + Cg;
  float B  = Y - Co - Cg;
  return vec3(R, G, B);
}

// ------------------------------------------------------------------------- //

// Perceptual tonemap for HDR variance clipping (operates in YCoCg space, Y = luminance)
vec3 tonemapYCoCg(vec3 c) {
  float Y = c.x;
  float scale = 1.0 / (1.0 + Y);
  return c * scale;
}

vec3 inverseTonemapYCoCg(vec3 c) {
  float Y = c.x;
  float scale = 1.0 / max(1.0 - Y, 0.001);
  return c * scale;
}

// based on https://www.shadertoy.com/view/4dSBDt
// color: raw YCoCg (not tonemapped) - tonemap is applied internally
void getVarianceClippingBounds(vec3 color, sampler2D colorSampler, ivec2 screenSpaceUV, float colorBoxSigma, out vec3 colorMin, out vec3 colorMax) {
  vec3 tm = tonemapYCoCg(color);
  vec3 colorAvg = tm;
  vec3 colorVar = tm * tm;

  // Marco Salvi's Implementation (by Chris Wyman)
  // Coordinates are clamped: an unclamped texelFetch past the edge is undefined per spec, and
  // in practice returns black, which widens the box along the screen border.
  ivec2 maxCoord = textureSize(colorSampler, 0) - ivec2(1);

  for (int y = -1; y <= 1; ++y)
  {
    for (int x = -1; x <= 1; ++x)
    {
      // Centre is already seeded from the caller's colour, which may be filtered
      if (x == 0 && y == 0) continue;

      vec3 fetch = texelFetch(colorSampler, clamp(screenSpaceUV + ivec2(x, y), ivec2(0), maxCoord), 0).rgb;
      fetch = tonemapYCoCg(rgbToYCoCg(fetch));
      colorAvg += fetch;
      colorVar += fetch * fetch;
    }
  }

  colorAvg *= (1.0 / 9.0);
  colorVar *= (1.0 / 9.0);

  vec3 sigma = sqrt(max(vec3(0.0), colorVar - colorAvg * colorAvg));
  colorMin = colorAvg - colorBoxSigma * sigma;
  colorMax = colorAvg + colorBoxSigma * sigma;

  // Ensure center pixel is always within bounds to prevent spatial bias
  colorMin = min(colorMin, tm);
  colorMax = max(colorMax, tm);
}
