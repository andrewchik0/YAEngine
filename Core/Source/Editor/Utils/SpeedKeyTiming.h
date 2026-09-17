#pragma once

#ifdef YA_EDITOR

#include "Editor/Utils/KeyRangeTiming.h"
#include "Utils/MotionPath.h"

// Moving and stretching a range of motion path speed keys so the drive still covers the same road. After the key
// times change, the speeds of some of the range's keys are scaled by one factor that keeps the road driven across a
// region around the range, so the drive outside it plays as before, only shifted in time: a stretched range is the
// same stretch of road driven slower.
namespace YAEngine::SpeedKeyTiming
{
  struct Result
  {
    // Delta (move) or duration (stretch) after clamping
    float applied = 0.0f;
    // Factor the scaled keys' speeds were multiplied by
    float speedScale = 1.0f;
  };

  // Keys scaledFirst..scaledLast get the factor; the road from regionFrom to regionTo is kept
  struct ScaleSpan
  {
    size_t scaledFirst = 0;
    size_t scaledLast = 0;
    size_t regionFrom = 0;
    size_t regionTo = 0;
  };

  // Scales the speeds of span's keys in keys, whose times were edited from original and whose speeds are still
  // original's, so the road across the span's region matches original. The speed blends with a smoothstep between
  // two keys, whose integral is half the span times the sum of both speeds, so the road is linear in the factor.
  // False, with the speeds left as they were, when the factor would fall outside [minScale, maxSpeed / the fastest
  // scaled speed]. Keys standing still have nothing to scale and are accepted as they are.
  inline bool KeepDriveDistance(const std::vector<MotionSpeedKey>& original, std::vector<MotionSpeedKey>& keys,
    const ScaleSpan& span, float minScale, float maxSpeed, float& scale)
  {
    scale = 1.0f;
    double target = 0.0;
    double fixed = 0.0;
    double scaled = 0.0;
    for (size_t i = span.regionFrom; i < span.regionTo; i++)
    {
      target += 0.5 * double(original[i + 1].time - original[i].time) * double(original[i].speed + original[i + 1].speed);
      const double halfSpan = 0.5 * double(keys[i + 1].time - keys[i].time);
      for (size_t end : { i, i + 1 })
      {
        const double part = halfSpan * double(original[end].speed);
        if (end >= span.scaledFirst && end <= span.scaledLast)
          scaled += part;
        else
          fixed += part;
      }
    }

    float fastest = 0.0f;
    for (size_t i = span.scaledFirst; i <= span.scaledLast; i++)
      fastest = std::max(fastest, original[i].speed);
    if (scaled <= 1e-9)
      return true;

    const double factor = (target - fixed) / scaled;
    const double maxScale = fastest > 0.0f ? double(maxSpeed) / double(fastest) : 1e9;
    // The tolerance keeps an unchanged range acceptable whatever the rounding
    if (!std::isfinite(factor) || factor < double(minScale) || factor > maxScale * (1.0 + 1e-5))
      return false;

    scale = float(factor);
    for (size_t i = span.scaledFirst; i <= span.scaledLast; i++)
      keys[i].speed = original[i].speed * scale;
    return true;
  }

  namespace Detail
  {
    // Every key of first..last scaled, the road kept from the key before the range to the key after it
    inline ScaleSpan WholeRange(size_t count, size_t first, size_t last)
    {
      return { first, last, first > 0 ? first - 1 : first, last + 1 < count ? last + 1 : last };
    }

    // Applies edit(keys, value) to a copy of the keys and keeps the road with the first span that can absorb it.
    // A value none can absorb is bisected back toward identity, which always can.
    template<typename Edit>
    Result EditKeepingRoad(std::vector<MotionSpeedKey>& keys, const std::vector<ScaleSpan>& spans, float value,
      float identity, float minScale, float maxSpeed, const Edit& edit)
    {
      const std::vector<MotionSpeedKey> original = keys;
      Result result;
      auto attempt = [&](float candidate) {
        for (const ScaleSpan& span : spans)
        {
          keys = original;
          const float applied = edit(keys, candidate);
          float scale = 1.0f;
          if (KeepDriveDistance(original, keys, span, minScale, maxSpeed, scale))
          {
            result = { applied, scale };
            return true;
          }
        }
        return false;
      };

      if (attempt(value))
        return result;

      float good = identity;
      float bad = value;
      for (int i = 0; i < 32 && std::abs(bad - good) > 1e-5f; i++)
      {
        const float middle = 0.5f * (good + bad);
        if (attempt(middle))
          good = middle;
        else
          bad = middle;
      }
      if (!attempt(good))
      {
        keys = original;
        result = { identity, 1.0f };
      }
      return result;
    }
  }

  // KeyRangeTiming::MoveKeyRange that keeps the drive's road: every key of the range is scaled and the road from the
  // key before the range to the key after it stays
  inline Result MoveSpeedKeyRange(std::vector<MotionSpeedKey>& keys, size_t first, size_t last, float delta,
    float minGap, float maxAdvance, float minScale, float maxSpeed)
  {
    if (!KeyRangeTiming::IsValidRange(keys, first, last) || std::isnan(delta))
      return {};
    const std::vector<ScaleSpan> spans = { Detail::WholeRange(keys.size(), first, last) };
    return Detail::EditKeepingRoad(keys, spans, delta, 0.0f, minScale, maxSpeed,
      [&](std::vector<MotionSpeedKey>& edited, float value) {
        return KeyRangeTiming::MoveKeyRange(edited, first, last, value, minGap, maxAdvance);
      });
  }

  // KeyRangeTiming::StretchKeyRange that keeps the drive's road. The key at the anchor keeps its speed, so the drive
  // on that side of the range stays exactly as it was (anchor start: up to and including the first key; anchor end:
  // from the last key on); the other keys of the range are scaled, keeping the road up to the neighbour on the moving
  // side. When that cannot absorb the edit (e.g. a braking pair stretched far), every key of the range is scaled
  // instead, which also bends the segments on both sides.
  inline Result StretchSpeedKeyRange(std::vector<MotionSpeedKey>& keys, size_t first, size_t last, float duration,
    KeyRangeTiming::StretchAnchor anchor, float minGap, float maxAdvance, float minScale, float maxSpeed)
  {
    if (!KeyRangeTiming::IsValidRange(keys, first, last))
      return {};
    const float current = keys[last].time - keys[first].time;
    if (std::isnan(duration))
      return { current, 1.0f };

    const size_t count = keys.size();
    const ScaleSpan anchored = anchor == KeyRangeTiming::StretchAnchor::Start
      ? ScaleSpan { first + 1, last, first, last + 1 < count ? last + 1 : last }
      : ScaleSpan { first, last - 1, first > 0 ? first - 1 : first, last };
    const std::vector<ScaleSpan> spans = { anchored, Detail::WholeRange(count, first, last) };
    return Detail::EditKeepingRoad(keys, spans, duration, current, minScale, maxSpeed,
      [&](std::vector<MotionSpeedKey>& edited, float value) {
        return KeyRangeTiming::StretchKeyRange(edited, first, last, value, anchor, minGap, maxAdvance);
      });
  }
}

#endif
