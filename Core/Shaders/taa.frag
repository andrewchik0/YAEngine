layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

#include "common.glsl"

layout(set = 1, binding = 0) uniform sampler2D frame;
layout(set = 1, binding = 1) uniform sampler2D history;
layout(set = 1, binding = 2) uniform sampler2D velocityTexture;

#include "variance_clipping.glsl"

void main()
{
  // Passthrough when TAA is disabled
  if (u_Frame.taaEnabled == 0)
  {
    outColor = vec4(texture(frame, uv).rgb, 1.0);
    return;
  }

  vec2 unjitteredUV = uv - vec2(u_Frame.jitterX, u_Frame.jitterY) * 0.5;
  ivec2 screenSpaceUV = ivec2(unjitteredUV * vec2(u_Frame.screenWidth, u_Frame.screenHeight));

  vec3 currentColor = texture(frame, unjitteredUV).rgb;
  vec3 currentYCoCg = rgbToYCoCg(currentColor);

  vec3 colorMin = vec3(0);
  vec3 colorMax = vec3(0);
  getVarianceClippingBounds(currentYCoCg, frame, screenSpaceUV, VARIANCE_CLIPPING_COLOR_BOX_SIGMA, colorMin, colorMax);

  vec2 velocity = texture(velocityTexture, uv).rg;
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
