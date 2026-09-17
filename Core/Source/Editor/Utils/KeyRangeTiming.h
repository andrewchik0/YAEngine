#pragma once

#ifdef YA_EDITOR

// Moving and stretching a range of timeline keys in time. Pure (STL only) and generic over any key
// with a float `time`, sorted with neighbours at least minGap apart; every function keeps that
// invariant without resorting and touches nothing but the times.
namespace YAEngine::KeyRangeTiming
{
  enum class StretchAnchor : uint8_t
  {
    // The range start stays; keys after the range shift with its end
    Start,
    // The range end stays; keys before the range stay too
    End
  };

  template<typename Key>
  bool IsValidRange(const std::vector<Key>& keys, size_t first, size_t last)
  {
    return first < last && last < keys.size();
  }

  // Shortest duration the range can be scaled to before two of its keys come closer than minGap. A range
  // already shorter than that (float rounding) can only keep its duration or grow.
  template<typename Key>
  float MinRangeDuration(const std::vector<Key>& keys, size_t first, size_t last, float minGap)
  {
    const float duration = keys[last].time - keys[first].time;
    float smallestGap = duration;
    for (size_t i = first; i < last; i++)
      smallestGap = std::min(smallestGap, keys[i + 1].time - keys[i].time);
    if (smallestGap <= 0.0f)
      return duration;
    return std::min(duration, duration * minGap / smallestGap);
  }

  // Shifts first..last by delta, clamped so the range stays minGap from its neighbours, never starts
  // below 0 and moves at most maxAdvance past a last key. Returns the delta applied.
  template<typename Key>
  float MoveKeyRange(std::vector<Key>& keys, size_t first, size_t last, float delta, float minGap, float maxAdvance)
  {
    if (!IsValidRange(keys, first, last) || std::isnan(delta))
      return 0.0f;

    const float start = keys[first].time;
    const float end = keys[last].time;
    const float lower = first > 0 ? keys[first - 1].time + minGap : 0.0f;
    const float upper = last + 1 < keys.size() ? keys[last + 1].time - minGap : end + maxAdvance;
    // Zero stays allowed, so rounding in the neighbour gaps never forces a move
    const float applied = std::clamp(delta, std::min(lower - start, 0.0f), std::max(upper - end, 0.0f));
    for (size_t i = first; i <= last; i++)
      keys[i].time += applied;
    return applied;
  }

  // Scales first..last to duration around the anchor; inner keys keep their relative spacing. Anchor
  // Start: the keys after the range shift by the change of the range end, and the growth is limited to
  // maxAdvance. Anchor End: the range start is clamped to minGap after the previous key (0 without
  // one). Returns the duration applied.
  template<typename Key>
  float StretchKeyRange(std::vector<Key>& keys, size_t first, size_t last, float duration, StretchAnchor anchor,
    float minGap, float maxAdvance)
  {
    if (!IsValidRange(keys, first, last))
      return 0.0f;

    const float start = keys[first].time;
    const float end = keys[last].time;
    const float oldDuration = end - start;
    if (std::isnan(duration) || oldDuration <= 0.0f)
      return oldDuration;

    const bool fromStart = anchor == StretchAnchor::Start;
    const float lower = first > 0 ? keys[first - 1].time + minGap : 0.0f;
    const float maxDuration = fromStart ? oldDuration + maxAdvance : std::max(end - lower, oldDuration);
    const float applied = std::clamp(duration, MinRangeDuration(keys, first, last, minGap), maxDuration);
    const float scale = applied / oldDuration;

    if (fromStart)
    {
      for (size_t i = first + 1; i < last; i++)
        keys[i].time = start + (keys[i].time - start) * scale;
      const float shift = start + applied - end;
      keys[last].time = start + applied;
      for (size_t i = last + 1; i < keys.size(); i++)
        keys[i].time += shift;
    }
    else
    {
      for (size_t i = first + 1; i < last; i++)
        keys[i].time = end - (end - keys[i].time) * scale;
      keys[first].time = end - applied;
    }
    return applied;
  }
}

#endif
