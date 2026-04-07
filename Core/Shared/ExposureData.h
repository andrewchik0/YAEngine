#ifdef __cplusplus
#pragma once
namespace YAEngine {
#endif

#define HISTOGRAM_BIN_COUNT 256
#define HISTOGRAM_MIN_LOG_LUM -8.0
#define HISTOGRAM_MAX_LOG_LUM 4.0

struct ExposureAdaptPushConstants
{
  float lowPercentile;
  float highPercentile;
  float adaptSpeedUp;
  float adaptSpeedDown;
  float deltaTime;
};

#ifdef __cplusplus
} // namespace YAEngine
#endif
