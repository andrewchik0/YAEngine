#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D frame;
layout(set = 0, binding = 1) uniform sampler2D history;

#include "denoise.glsl"

void main()
{
  vec2 normalizedSpaceUV = uv;
  vec3 new = SAMPLE_RGB(frame, normalizedSpaceUV);

  new = RGBToYCoCg(new);

  vec3 colorMin = vec3(0);
  vec3 colorMax = vec3(0);
  getVarianceClippingBounds(new, frame, ivec2(normalizedSpaceUV * vec2(2560, 1360)), VARIANCE_CLIPPING_COLOR_BOX_SIGMA, colorMin, colorMax);

  vec3 historyCol = SAMPLE_RGB(history, normalizedSpaceUV);
  historyCol = RGBToYCoCg(historyCol);

  vec3 filtered = vec3(0);
  historyCol = clamp(historyCol, colorMin, colorMax);

  filtered = mix(new, historyCol, EMA_IIR_INVERSE_CUTOFF_FREQUENCY);
  filtered = YCoCgToRGB(filtered);

  vec3 result = vec3(0);

  for (int i = -1; i <= 1; i++)
  {
    for (int j = -1; j <= 1; j++)
    {
      vec2 xy = uv;
      xy += vec2(i, j) / vec2(2560, 1360);
      result += mix(filtered, texture(history, xy).rgb, 0.5);
    }
  }

  result /= 9;
  outColor = vec4(mix(filtered, texture(history, uv).rgb, 0.5).rgb, 1.0);
}
