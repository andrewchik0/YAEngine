#ifdef __cplusplus
#pragma once
#define vec2 glm::vec2
namespace YAEngine {
#endif

// Ported from XeGTAO by Intel (SPDX-License-Identifier: MIT, https://github.com/GameTechDev/XeGTAO),
// which implements GTAO/GTSO from Jimenez et al., "Practical Real-Time Strategies for Accurate
// Indirect Occlusion" (Activision, 2016).

// Hard-coded in the prefilter shader: one dispatch builds exactly this many mips.
#define GTAO_DEPTH_MIP_LEVELS 5

// Raw pre-denoise visibility can overshoot 1 and only averages back down during the denoise,
// so it is divided by this before being stored in UNORM and multiplied back on final apply.
#define GTAO_OCCLUSION_TERM_SCALE 1.5

// Hilbert curve LUT that drives the R2 noise sequence. 64x64, so indices fit in 12 bits.
#define GTAO_HILBERT_LEVEL 6
#define GTAO_HILBERT_WIDTH 64

// Slice and step counts per quality level, matching the four XeGTAO entry points.
#define GTAO_QUALITY_LOW    0
#define GTAO_QUALITY_MEDIUM 1
#define GTAO_QUALITY_HIGH   2
#define GTAO_QUALITY_ULTRA  3

struct GTAOConstants
{
  vec2 viewportPixelSize;
  // Screen UV to view space ray scale/offset. Encodes the projection, so GTAO never touches
  // the reversed-Z depth buffer or the projection matrix directly.
  vec2 ndcToViewMul;
  vec2 ndcToViewAdd;
  vec2 ndcToViewMulPixelSize;

  float effectRadius;
  float effectFalloffRange;
  float radiusMultiplier;
  float finalValuePower;

  float denoiseBlurBeta;
  float sampleDistributionPower;
  float thinOccluderCompensation;
  float depthMipSamplingOffset;

  int noiseIndex;
  int sliceCount;
  int stepsPerSlice;
  int padding0;

  // SSGI (visibility bitmask) extension of the same pass. Radius and thickness are
  // world-space meters; intensity is an artistic multiplier on the screen-gathered
  // irradiance only, never on the volume fallback.
  float ssgiRadius;
  float ssgiThickness;
  float ssgiIntensity;
  int padding1;
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec2
#endif
