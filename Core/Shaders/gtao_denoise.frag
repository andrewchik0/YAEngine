layout(location = 0) in vec2 uv;
layout(location = 0) out float outAO;

#include "common.glsl"
#include "gtao_common.glsl"

layout(set = 1, binding = 1) uniform sampler2D aoTexture;
layout(set = 1, binding = 2) uniform sampler2D edgesTexture;

// Edge-aware blur that weights neighbours by the depth-discontinuity strength the main pass
// already measured, so it never needs to re-read depth. Only one pass runs: the reference
// notes a single pass is enough once TAA is integrating the result, which it is here.
// Denoising is switched off by driving denoiseBlurBeta to a huge value, which makes the
// center weight dominate - same mechanism the reference uses.

ivec2 g_MaxCoord;

float LoadAO(ivec2 coord)
{
  return texelFetch(aoTexture, clamp(coord, ivec2(0), g_MaxCoord), 0).r;
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

  float sumWeight = blurAmount;
  float sum = LoadAO(pixCoord) * sumWeight;

  sum += LoadAO(pixCoord + ivec2(-1, 0)) * edgesC.x;
  sum += LoadAO(pixCoord + ivec2( 1, 0)) * edgesC.y;
  sum += LoadAO(pixCoord + ivec2( 0, -1)) * edgesC.z;
  sum += LoadAO(pixCoord + ivec2( 0, 1)) * edgesC.w;
  sumWeight += edgesC.x + edgesC.y + edgesC.z + edgesC.w;

  sum += LoadAO(pixCoord + ivec2(-1, -1)) * weightTL;
  sum += LoadAO(pixCoord + ivec2( 1, -1)) * weightTR;
  sum += LoadAO(pixCoord + ivec2(-1, 1)) * weightBL;
  sum += LoadAO(pixCoord + ivec2( 1, 1)) * weightBR;
  sumWeight += weightTL + weightTR + weightBL + weightBR;

  // Undo the packing headroom the main pass applied. The target is UNORM, so the values that
  // overshot 1 before averaging clamp here exactly as they do in the reference.
  outAO = clamp((sum / sumWeight) * float(GTAO_OCCLUSION_TERM_SCALE), 0.0, 1.0);
}
