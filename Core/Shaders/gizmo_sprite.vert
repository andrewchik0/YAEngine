#version 450

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outColor;

#include "common.glsl"
#include "../Shared/SpritePushConstants.h"

layout(push_constant) uniform PushConstantBlock { SpritePushConstants pc; };

void main()
{
  // 4-vertex triangle strip: 0=BL, 1=BR, 2=TL, 3=TR
  vec2 corners[4] = vec2[](
    vec2(-0.5, -0.5),
    vec2( 0.5, -0.5),
    vec2(-0.5,  0.5),
    vec2( 0.5,  0.5)
  );

  vec2 uvs[4] = vec2[](
    vec2(0.0, 1.0),
    vec2(1.0, 1.0),
    vec2(0.0, 0.0),
    vec2(1.0, 0.0)
  );

  vec2 corner = corners[gl_VertexIndex];
  corner.x *= pc.shadow.y; // aspect ratio correction
  float size = pc.positionAndScale.w;

  // Extract camera right and up from view matrix
  vec3 right = vec3(u_Frame.view[0][0], u_Frame.view[1][0], u_Frame.view[2][0]);
  vec3 up    = vec3(u_Frame.view[0][1], u_Frame.view[1][1], u_Frame.view[2][1]);

  vec3 worldPos = pc.positionAndScale.xyz + (right * corner.x + up * corner.y) * size;

  gl_Position = u_Frame.proj * u_Frame.view * vec4(worldPos, 1.0);
  gl_Position.xy += vec2(u_Frame.jitterX, u_Frame.jitterY) * gl_Position.w;
  outUV = uvs[gl_VertexIndex];
  outColor = pc.color;
}
