layout(location = 0) in vec2 uv;
layout(location = 0) out float outAO;
layout(location = 1) out vec4 outGI;
layout(location = 2) out vec2 outBentNormal;

#include "common.glsl"
#include "octahedron.glsl"
#include "gtao_common.glsl"

layout(set = 1, binding = 1) uniform sampler2D aoTexture;
layout(set = 1, binding = 2) uniform sampler2D edgesTexture;
layout(set = 1, binding = 3) uniform sampler2D giTexture;
layout(set = 1, binding = 4) uniform sampler2D bentTexture;

// Edge-aware blur that weights neighbours by the depth-discontinuity strength the main pass
// already measured, so it never needs to re-read depth. Only one pass runs: the reference
// notes a single pass is enough once TAA is integrating the result, which it is here.
// Denoising is switched off by driving denoiseBlurBeta to a huge value, which makes the
// center weight dominate - same mechanism the reference uses.
//
// The SSGI channels ride the SAME weights on purpose, and it is a correctness
// requirement, not an optimization: the screen part and the fallback weight in
// giTexture must be blurred by one kernel, or their sum stops being a full
// hemisphere along silhouettes and a bright or dark fringe appears.

ivec2 g_MaxCoord;

float LoadAO(ivec2 coord)
{
  return texelFetch(aoTexture, clamp(coord, ivec2(0), g_MaxCoord), 0).r;
}

vec4 LoadGI(ivec2 coord)
{
  return texelFetch(giTexture, clamp(coord, ivec2(0), g_MaxCoord), 0);
}

vec3 LoadBent(ivec2 coord)
{
  return octDecode(texelFetch(bentTexture, clamp(coord, ivec2(0), g_MaxCoord), 0).rg * 2.0 - 1.0);
}

vec4 LoadEdges(ivec2 coord)
{
  return gtaoUnpackEdges(texelFetch(edgesTexture, clamp(coord, ivec2(0), g_MaxCoord), 0).r);
}

void main()
{
  ivec2 pixCoord = ivec2(gl_FragCoord.xy);
  g_MaxCoord = ivec2(u_Frame.screenWidth, u_Frame.screenHeight) - 1;

  const float diagWeight = 0.85 * 0.5;
  float blurAmount = u_GTAO.denoiseBlurBeta;

  vec4 edgesL = LoadEdges(pixCoord + ivec2(-1, 0));
  vec4 edgesR = LoadEdges(pixCoord + ivec2( 1, 0));
  vec4 edgesT = LoadEdges(pixCoord + ivec2( 0, -1));
  vec4 edgesB = LoadEdges(pixCoord + ivec2( 0, 1));
  vec4 edgesC = LoadEdges(pixCoord);

  // Edge detection is not symmetric: a left edge on the right pixel need not match the right
  // edge on the left pixel. Multiplying the pair together forces symmetry, which sharpens the
  // blur and stops it from crawling under TAA.
  edgesC *= vec4(edgesL.y, edgesR.x, edgesT.w, edgesB.z);

  // Where three or four sides are edges the pixel would keep its own noise entirely, so a
  // small amount of leaking is allowed back in to cut spatial and temporal aliasing.
  const float leakThreshold = 2.5;
  const float leakStrength = 0.5;
  float edginess = (clamp(4.0 - leakThreshold - dot(edgesC, vec4(1.0)), 0.0, 1.0)
    / (4.0 - leakThreshold)) * leakStrength;
  edgesC = clamp(edgesC + edginess, 0.0, 1.0);

  float weightTL = diagWeight * (edgesC.x * edgesL.z + edgesC.z * edgesT.x);
  float weightTR = diagWeight * (edgesC.z * edgesT.y + edgesC.y * edgesR.z);
  float weightBL = diagWeight * (edgesC.w * edgesB.x + edgesC.x * edgesL.w);
  float weightBR = diagWeight * (edgesC.y * edgesR.w + edgesC.w * edgesB.y);

  // The nine taps and their weights, shared by every channel below.
  ivec2 offsets[9] = ivec2[9](
    ivec2(0, 0),
    ivec2(-1, 0), ivec2(1, 0), ivec2(0, -1), ivec2(0, 1),
    ivec2(-1, -1), ivec2(1, -1), ivec2(-1, 1), ivec2(1, 1));
  float weights[9] = float[9](
    blurAmount,
    edgesC.x, edgesC.y, edgesC.z, edgesC.w,
    weightTL, weightTR, weightBL, weightBR);

  float sumWeight = 0.0;
  float sumAO = 0.0;
  for (int i = 0; i < 9; i++)
  {
    sumAO += LoadAO(pixCoord + offsets[i]) * weights[i];
    sumWeight += weights[i];
  }

  // Undo the packing headroom the main pass applied. The target is UNORM, so the values that
  // overshot 1 before averaging clamp here exactly as they do in the reference.
  outAO = clamp((sumAO / sumWeight) * float(GTAO_OCCLUSION_TERM_SCALE), 0.0, 1.0);

  // The SSGI targets hold zeros while SSGI is off - skip their eighteen loads.
  if (u_Frame.ssgiEnabled == 0)
  {
    outGI = vec4(0.0);
    outBentNormal = texelFetch(bentTexture, pixCoord, 0).rg;
    return;
  }

  vec4 sumGI = vec4(0.0);
  vec3 sumBent = vec3(0.0);
  for (int i = 0; i < 9; i++)
  {
    sumGI += LoadGI(pixCoord + offsets[i]) * weights[i];
    sumBent += LoadBent(pixCoord + offsets[i]) * weights[i];
  }

  outGI = sumGI / sumWeight;
  // Decoded before averaging and re-normalized after: octahedral coordinates are
  // not linear across seams, and the consumer expects a unit direction.
  float bentLen = length(sumBent);
  vec3 bent = bentLen > 1e-5 ? sumBent / bentLen : vec3(0.0, 1.0, 0.0);
  outBentNormal = octEncode(bent) * 0.5 + 0.5;
}
