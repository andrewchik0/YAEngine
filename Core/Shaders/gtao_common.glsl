#ifndef GTAO_COMMON_GLSL
#define GTAO_COMMON_GLSL

// Ported from XeGTAO by Intel (SPDX-License-Identifier: MIT, https://github.com/GameTechDev/XeGTAO).
// Algorithm: Jimenez et al., "Practical Real-Time Strategies for Accurate Indirect Occlusion".
//
// Deviation from the reference: it gathers with GatherRed, this port uses explicit texelFetch
// neighbourhoods instead. Component ordering of a gather is the one thing that does not survive
// an HLSL-to-GLSL translation by inspection, and every pass here is neighbourhood-bound anyway.

// Requires common.glsl (for u_Frame) to be included before this file.
#include "../Shared/GTAOConstants.h"
layout(set = 1, binding = 0) uniform GTAOConstantsBlock { GTAOConstants u_GTAO; };

const float GTAO_PI = 3.1415926535897932;
const float GTAO_PI_HALF = 1.5707963267948966;

// fp16 storage limit for the prefiltered depth: sky texels linearize to a huge finite value
// and would otherwise become inf, and inf - inf is NaN in every delta below.
const float GTAO_DEPTH_CLAMP = 65504.0;

// GTAO works in its own view space - X right, Y up the screen, Z forward and positive -
// which is the engine view space mirrored through diag(1, 1, -1). Only Z flips because the
// engine looks down -Z while GTAO looks down +Z; Y already points up the screen in both,
// since the projection Render uploads carries the Vulkan Y flip.
//
// The handedness change is safe: the only cross product in the main pass builds axisVec,
// which is used solely to project a component out of the normal, so its sign cancels.
//
// Only two things enter this space: positions built here from screen UV plus linear depth,
// and the surface normal, which the main pass mirrors the same way. Nothing else sees them.
vec3 gtaoViewspacePosition(vec2 screenUV, float viewspaceDepth)
{
  return vec3((u_GTAO.ndcToViewMul * screenUV + u_GTAO.ndcToViewAdd) * viewspaceDepth,
    viewspaceDepth);
}

// Input [-1, 1], output [0, PI]. From Lagarde, "Inverse trigonometric functions GPU optimization
// for AMD GCN architecture". The reference pairs this with a bit-hack rsqrt; a plain sqrt is
// close enough and keeps the shader free of float/int aliasing.
float gtaoFastAcos(float inX)
{
  float x = abs(inX);
  float res = -0.156583 * x + GTAO_PI_HALF;
  res *= sqrt(1.0 - x);
  return (inX >= 0.0) ? res : GTAO_PI - res;
}

// Depth discontinuity strength towards each of the four neighbours, 1 = continuous surface.
// The slope-adjusted variant keeps a steeply inclined but continuous surface from reading as
// an edge: on a flat slope the second difference cancels while a real silhouette survives.
vec4 gtaoCalculateEdges(float centerZ, float leftZ, float rightZ, float topZ, float bottomZ)
{
  vec4 edgesLRTB = vec4(leftZ, rightZ, topZ, bottomZ) - centerZ;

  float slopeLR = (edgesLRTB.y - edgesLRTB.x) * 0.5;
  float slopeTB = (edgesLRTB.w - edgesLRTB.z) * 0.5;
  vec4 edgesLRTBSlopeAdjusted = edgesLRTB + vec4(slopeLR, -slopeLR, slopeTB, -slopeTB);
  edgesLRTB = min(abs(edgesLRTB), abs(edgesLRTBSlopeAdjusted));

  return clamp(1.25 - edgesLRTB / (centerZ * 0.011), 0.0, 1.0);
}

// 2 bits per edge, so four gradient steps (0, 1/3, 2/3, 1) rather than a binary edge mask.
float gtaoPackEdges(vec4 edgesLRTB)
{
  edgesLRTB = round(clamp(edgesLRTB, 0.0, 1.0) * 2.9);
  return dot(edgesLRTB, vec4(64.0 / 255.0, 16.0 / 255.0, 4.0 / 255.0, 1.0 / 255.0));
}

vec4 gtaoUnpackEdges(float packedVal)
{
  uint packed = uint(packedVal * 255.5);
  vec4 edgesLRTB;
  edgesLRTB.x = float((packed >> 6) & 0x03u) / 3.0;
  edgesLRTB.y = float((packed >> 4) & 0x03u) / 3.0;
  edgesLRTB.z = float((packed >> 2) & 0x03u) / 3.0;
  edgesLRTB.w = float((packed >> 0) & 0x03u) / 3.0;
  return clamp(edgesLRTB, 0.0, 1.0);
}

// Weighted 2x2 depth reduction for the mip chain. A plain average would drag a mip texel
// towards whatever is closest to the camera and make distant occluders vanish, so samples
// are weighted by the same falloff the main pass uses, relative to the nearest of the four.
float gtaoDepthMipFilter(float depth0, float depth1, float depth2, float depth3)
{
  float maxDepth = max(max(depth0, depth1), max(depth2, depth3));

  const float depthRangeScaleFactor = 0.75;
  float effectRadius = depthRangeScaleFactor * u_GTAO.effectRadius * u_GTAO.radiusMultiplier;
  float falloffRange = u_GTAO.effectFalloffRange * effectRadius;
  float falloffFrom = effectRadius * (1.0 - u_GTAO.effectFalloffRange);

  float falloffMul = -1.0 / falloffRange;
  float falloffAdd = falloffFrom / falloffRange + 1.0;

  vec4 depths = vec4(depth0, depth1, depth2, depth3);
  vec4 weights = clamp((maxDepth - depths) * falloffMul + falloffAdd, 0.0, 1.0);

  float weightSum = dot(weights, vec4(1.0));
  return dot(weights, depths) / max(weightSum, 1e-6);
}

#endif
