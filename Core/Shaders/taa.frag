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

void main()
{
  // Passthrough when TAA is disabled
  if (u_Frame.taaEnabled == 0)
  {
    outColor = vec4(texture(frame, uv).rgb, 1.0);
    return;
  }

  ivec2 screenSpaceUV = ivec2(gl_FragCoord.xy);

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

  ivec2 maxCoord = textureSize(frame, 0) - ivec2(1);
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

  vec3 colorMin = vec3(0);
  vec3 colorMax = vec3(0);
  getVarianceClippingBounds(currentYCoCg, frame, screenSpaceUV, u_Frame.taaClampSigma, colorMin, colorMax);

  float depth = texture(depthTexture, uv).r;
  vec2 velocity = depth <= BACKGROUND_DEPTH
    ? backgroundVelocity(uv * 2.0 - 1.0)
    : texture(velocityTexture, uv).rg;

  vec2 historyUV = uv - velocity;

  bool historyValid = historyUV.x >= 0.0 && historyUV.x <= 1.0
                   && historyUV.y >= 0.0 && historyUV.y <= 1.0;

  vec3 result;
  if (historyValid)
  {
    vec3 historyCol = texture(history, historyUV).rgb;
    historyCol = rgbToYCoCg(historyCol);
    historyCol = tonemapYCoCg(historyCol);
    historyCol = clamp(historyCol, colorMin, colorMax);
    historyCol = inverseTonemapYCoCg(historyCol);

    float speed = length(velocity * vec2(u_Frame.screenWidth, u_Frame.screenHeight));
    float blendFactor = mix(EMA_IIR_INVERSE_CUTOFF_FREQUENCY, 0.5, clamp(speed / 16.0, 0.0, 1.0));

    result = mix(currentYCoCg, historyCol, blendFactor);
    result = yCoCgToRGB(result);
  }
  else
  {
    result = currentColor;
  }

  outColor = vec4(result, 1.0);
}
