#ifdef __cplusplus
#pragma once
namespace YAEngine {
#endif

#define BLOOM_MIP_COUNT 6

struct BloomPushConstants
{
  int srcWidth;
  int srcHeight;
  int mipLevel;
  float filterRadius;
  float threshold;
  float softKnee;
};

#ifdef __cplusplus
} // namespace YAEngine
#endif
