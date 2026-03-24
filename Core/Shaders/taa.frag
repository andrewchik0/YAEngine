#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform PerFrameUBO {
  mat4 view;
  mat4 proj;
  mat4 invProj;
  mat4 prevView;
  mat4 prevProj;
  vec3 cameraPosition;
  float time;
  vec3 cameraDirection;
  float gamma;
  float exposure;
  int currentTexture;
  float near;
  float far;
  float fov;
  int screenWidth;
  int screenHeight;
  int ssaoEnabled;
  int ssrEnabled;
  int taaEnabled;
  float jitterX;
  float jitterY;
  int hizMipCount;
} u_Data;

layout(set = 1, binding = 0) uniform sampler2D frame;
layout(set = 1, binding = 1) uniform sampler2D history;
layout(set = 1, binding = 2) uniform sampler2D velocityTexture;

#include "denoise.glsl"

void main()
{
  // Passthrough when TAA is disabled
  if (u_Data.taaEnabled == 0)
  {
    outColor = vec4(texture(frame, uv).rgb, 1.0);
    return;
  }

  vec2 unjitteredUV = uv - vec2(u_Data.jitterX, u_Data.jitterY) * 0.5;
  ivec2 screenSpaceUV = ivec2(unjitteredUV * vec2(u_Data.screenWidth, u_Data.screenHeight));

  vec3 currentColor = texture(frame, unjitteredUV).rgb;
  vec3 currentYCoCg = RGBToYCoCg(currentColor);

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
    historyCol = RGBToYCoCg(historyCol);
    historyCol = TonemapYCoCg(historyCol);
    historyCol = clamp(historyCol, colorMin, colorMax);
    historyCol = InverseTonemapYCoCg(historyCol);

    float speed = length(velocity * vec2(u_Data.screenWidth, u_Data.screenHeight));
    float blendFactor = mix(EMA_IIR_INVERSE_CUTOFF_FREQUENCY, 0.5, clamp(speed / 16.0, 0.0, 1.0));

    result = mix(currentYCoCg, historyCol, blendFactor);
    result = YCoCgToRGB(result);
  }
  else
  {
    result = currentColor;
  }

  outColor = vec4(result, 1.0);
}
