layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inPosition;
layout(location = 3) in mat3 inTBN;
layout(location = 6) in vec4 inCurClipPos;
layout(location = 7) in vec4 inPrevClipPos;

#include "common.glsl"

#include "material.glsl"

layout(location = 0) out vec4 outGBuffer0;
layout(location = 1) out vec4 outGBuffer1;
layout(location = 2) out vec2 outVelocity;

#include "octahedron.glsl"

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
    outGBuffer0 = vec4(albedo.rgb, 0.0);

    vec2 octNorm = octEncode(inNormal) * 0.5 + 0.5;
    float shadingModelUnlit = 1.0 / 3.0;
    outGBuffer1 = vec4(octNorm, 1.0, shadingModelUnlit);

    outVelocity = velocity;
  }
}
