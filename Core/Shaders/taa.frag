#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D frame;
layout(set = 0, binding = 1) uniform sampler2D history;

void main()
{
  vec3 result = vec3(0);
  for (int i = -1; i <= 1; i++)
  {
    for (int j = -1; j <= 1; j++)
    {
      vec2 xy = uv;
      xy.x /= 2650;
      xy.y /= 1300;

      vec3 historyColor = texture(history, uv).rgb;
      vec3 currColor = texture(frame, uv).rgb;

      result += mix(historyColor, currColor, 0.5);
    }
  }

  result /= 9;

  outColor = vec4(result, 1.0);
}
