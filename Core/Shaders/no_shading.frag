layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inPosition;
layout(location = 3) in mat3 inTBN;
layout(location = 6) in vec4 inCurClipPos;
layout(location = 7) in vec4 inPrevClipPos;

#include "common.glsl"

#include "material.glsl"

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec2 outMaterial;
layout(location = 3) out vec4 outAlbedo;
layout(location = 4) out vec2 outVelocity;

void main() {
  float base = float(u_Material.textureMask & 1);
  vec4 albedo = texture(baseColorTexture, inTexCoord) * base + vec4(u_Material.albedo, 1.0) * (1.0 - base);

  vec2 curNDC = inCurClipPos.xy / inCurClipPos.w;
  vec2 prevNDC = inPrevClipPos.xy / inPrevClipPos.w;
  vec2 velocity = (curNDC - prevNDC) * 0.5;

  if (albedo.a < 0.5)
  {
    discard;
  }
  else
  {
    outColor = vec4(albedo.rgb, 1.0);
    outNormal = vec4(inNormal, 1.0);
    outMaterial = vec2(1.0, 0.0);
    outAlbedo = vec4(albedo.rgb, 1.0);
    outVelocity = velocity;
  }
}
