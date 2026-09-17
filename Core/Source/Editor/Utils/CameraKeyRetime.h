#pragma once

#ifdef YA_EDITOR

#include "Utils/CameraTrack.h"

// Retiming of a camera key range so the camera's travel keeps pace with something: steady time, or
// the distance a car covers. Pure (STL/GLM only): the scene side lives in SequencerEditing.
namespace YAEngine::CameraKeyRetime
{
  // Chords per segment when measuring the evaluated curve; the error against the true arc stays
  // well below a percent for any segment a camera track produces
  inline constexpr int32_t SEGMENT_SAMPLES = 64;
  // New key times change the tangents and the car positions that the travel was measured with, so the
  // retime is repeated until a further pass would move no key by SETTLE_SECONDS. Measured on random
  // tracks, 32 passes settle about 98% of them; more passes barely add to that (the rest have no stable
  // timing at all).
  inline constexpr int32_t MAX_PASSES = 32;
  inline constexpr float SETTLE_SECONDS = 1e-3f;
  // Step scale after a pass that made the largest key move grow, halved on every such pass
  inline constexpr float MIN_STEP_SCALE = 0.125f;
  inline constexpr float MIN_TRAVEL = 1e-3f;
  inline constexpr float MIN_PACE = 1e-4f;
  inline constexpr int32_t PACE_SEARCH_STEPS = 32;

  // Frame of the follow target at a time; false while the track has none
  using FrameAt = std::function<bool(float time, CameraTargetFrame& out)>;

  // What the keys keep pace with: a progress that never decreases over time, and its inverse, the
  // first time at or after `after` at which the progress reaches `value` (infinity if it never does)
  struct Pace
  {
    std::function<float(float time)> progress;
    std::function<float(float value, float after)> timeAt;
  };

  enum class Error : uint8_t
  {
    None,
    TooFewKeys,
    TooShort,
    NoTravel,
    NoPace
  };

  struct Result
  {
    Error error = Error::None;
    // Times of the keys first..last after the retime
    std::vector<float> times;
    // Camera travel and pace progress over the segments that move, as measured in the pass the times
    // come from
    float travel = 0.0f;
    float pace = 0.0f;
    int32_t relativeSegments = 0;
    int32_t worldSegments = 0;
    int32_t passes = 0;
    // Largest key move of the pass the times come from. Settled: below SETTLE_SECONDS, so running the
    // retime again leaves the keys where they are. Otherwise the times are those of the pass that
    // moved the keys least.
    float shift = 0.0f;
    bool settled = false;
  };

  // Steady time: the camera covers equal distances in equal time
  inline Pace SteadyPace()
  {
    return {
      .progress = [](float time) { return time; },
      .timeAt = [](float value, float after) { return std::max(value, after); }
    };
  }

  // Where a segment's travel is measured. Relative: between two keys relative to a target that resolves,
  // in the target's frame, so the target's own motion (a car braking) is not camera travel. Mixed: one
  // key of each space; the evaluated curve drags the relative end along with the car, so its length
  // would grow with the time the segment gets and the retime would run away. It is measured as the
  // straight distance between the two keys' world positions at their own times instead.
  enum class SegmentSpace : uint8_t
  {
    World,
    Relative,
    Mixed
  };

  inline SegmentSpace GetSegmentSpace(const std::vector<CameraTrackKey>& keys, size_t s, bool hasTarget)
  {
    // Without a target the relative keys are read as world ones
    if (!hasTarget)
      return SegmentSpace::World;
    const bool from = keys[s].space == CameraKeySpace::Target;
    const bool to = keys[s + 1].space == CameraKeySpace::Target;
    return from && to ? SegmentSpace::Relative : from || to ? SegmentSpace::Mixed : SegmentSpace::World;
  }

  inline glm::vec3 KeyWorldPosition(const CameraTrackKey& key, const FrameAt& frameAt)
  {
    CameraTargetFrame frame;
    if (key.space == CameraKeySpace::Target && frameAt(key.time, frame))
      return frame.rotation * key.position + frame.position;
    return key.position;
  }

  // Camera travel from keys[s] to keys[s + 1] with these key times: the path length of the curve
  // EvaluateCameraTrack moves the camera along, except for a mixed segment (see SegmentSpace)
  inline float SegmentTravel(const std::vector<CameraTrackKey>& keys, size_t s, const FrameAt& frameAt,
    SegmentSpace space)
  {
    if (space == SegmentSpace::Mixed)
      return glm::distance(KeyWorldPosition(keys[s], frameAt), KeyWorldPosition(keys[s + 1], frameAt));

    const bool relative = space == SegmentSpace::Relative;
    const float start = keys[s].time;
    const float end = keys[s + 1].time;
    float travel = 0.0f;
    glm::vec3 previous(0.0f);
    for (int32_t i = 0; i <= SEGMENT_SAMPLES; i++)
    {
      const float t = glm::mix(start, end, float(i) / float(SEGMENT_SAMPLES));
      CameraTargetFrame frame;
      const bool hasFrame = frameAt(t, frame);
      glm::vec3 position = EvaluateCameraTrack(keys, t, hasFrame ? &frame : nullptr).position;
      if (relative && hasFrame)
        position = glm::inverse(frame.rotation) * (position - frame.position);
      if (i > 0)
        travel += glm::distance(position, previous);
      previous = position;
    }
    return travel;
  }

  // Moves the keys strictly between first and last so the camera's travel is spread the way the pace
  // progresses: the camera has covered a share of its travel when the pace has covered the same share.
  // The first and last key keep their times, and a Hold segment keeps its duration (the pace it spans
  // is left out). Neighbouring keys stay at least minGap apart.
  inline Result Retime(std::vector<CameraTrackKey> keys, size_t first, size_t last, float minGap,
    const FrameAt& frameAt, const Pace& pace)
  {
    Result result;
    if (last >= keys.size() || first + 2 > last)
    {
      result.error = Error::TooFewKeys;
      return result;
    }

    const size_t count = last - first + 1;
    const float start = keys[first].time;
    const float end = keys[last].time;
    if (end - start < minGap * float(count - 1))
    {
      result.error = Error::TooShort;
      return result;
    }

    const float fullPace = pace.progress(end) - pace.progress(start);
    if (fullPace < MIN_PACE)
    {
      result.error = Error::NoPace;
      return result;
    }

    CameraTargetFrame probe;
    const bool hasTarget = frameAt(start, probe);
    std::vector<float> travel(count - 1);
    std::vector<float> walked(count);

    struct Pass
    {
      float travel = 0.0f;
      float pace = 0.0f;
      int32_t relativeSegments = 0;
      int32_t worldSegments = 0;
    };

    // One plain retime: the times of first..last derived from the travel the keys have with their
    // current times. False while the camera does not move.
    auto retimeOnce = [&](std::vector<float>& out, Pass& pass) {
      pass = {};
      for (size_t s = first; s < last; s++)
      {
        // Negative marks a held segment
        float& length = travel[s - first];
        if (keys[s].interpOut == CameraKeyInterp::Hold)
        {
          length = -1.0f;
          continue;
        }

        const SegmentSpace space = GetSegmentSpace(keys, s, hasTarget);
        (space == SegmentSpace::Relative ? pass.relativeSegments : pass.worldSegments)++;
        length = SegmentTravel(keys, s, frameAt, space);
        pass.travel += length;
      }
      if (pass.travel < MIN_TRAVEL)
        return false;

      // Lays the keys out with the moving segments sharing `share` of the pace by their travel; returns
      // when the last key would come
      auto walk = [&](float share) {
        walked[0] = start;
        for (size_t i = 0; i + 1 < count; i++)
        {
          const size_t s = first + i;
          walked[i + 1] = travel[i] < 0.0f ? walked[i] + (keys[s + 1].time - keys[s].time)
            : pace.timeAt(pace.progress(walked[i]) + share * travel[i] / pass.travel, walked[i]);
          if (!std::isfinite(walked[i + 1]))
            return walked[i + 1];
        }
        return walked[count - 1];
      };

      // Which pace the held segments leave to the moving ones depends on where the holds land, so it is
      // searched for: the walk ends later the more pace it hands out. A pace that stops before the end
      // (a car parked early) leaves the last segment to cover the standstill.
      pass.pace = fullPace;
      if (walk(fullPace) > end)
      {
        float low = 0.0f;
        float high = fullPace;
        for (int32_t step = 0; step < PACE_SEARCH_STEPS; step++)
        {
          const float middle = 0.5f * (low + high);
          if (walk(middle) <= end)
            low = middle;
          else
            high = middle;
        }
        pass.pace = low;
      }
      walk(pass.pace);
      out = walked;
      return true;
    };

    // Keys standing on the same spot would otherwise share a time
    auto keepOrder = [&](std::vector<float>& times) {
      times[0] = start;
      times[count - 1] = end;
      for (size_t i = 1; i + 1 < count; i++)
        times[i] = std::max(times[i], times[i - 1] + minGap);
      for (size_t i = count - 2; i >= 1; i--)
        times[i] = std::min(times[i], times[i + 1] - minGap);
    };

    // x: the key times a pass measures with, g: how far that pass would move them. Plain passes (x
    // becomes x + g) creep when a segment's travel grows with the time it gets and overshoot when it
    // shrinks, so the step is Anderson-accelerated (depth 1): the last two passes estimate how g
    // responds to x. A pass whose largest move grew drops that estimate and takes a shorter plain step.
    std::vector<float> x(count);
    std::vector<float> derived(count);
    std::vector<float> g(count, 0.0f);
    std::vector<float> next(count);
    std::vector<float> previousX;
    std::vector<float> previousG;
    float previousShift = std::numeric_limits<float>::infinity();
    float stepScale = 1.0f;
    for (size_t i = 0; i < count; i++)
      x[i] = keys[first + i].time;

    for (int32_t passIndex = 0; passIndex < MAX_PASSES; passIndex++)
    {
      Pass pass;
      if (!retimeOnce(derived, pass))
      {
        if (passIndex == 0)
        {
          result.error = Error::NoTravel;
          return result;
        }
        break;
      }
      keepOrder(derived);

      float shift = 0.0f;
      for (size_t i = 1; i + 1 < count; i++)
      {
        g[i] = derived[i] - x[i];
        shift = std::max(shift, std::abs(g[i]));
      }

      result.passes = passIndex + 1;
      if (result.times.empty() || shift < result.shift)
      {
        result.times = derived;
        result.shift = shift;
        result.travel = pass.travel;
        result.pace = pass.pace;
        result.relativeSegments = pass.relativeSegments;
        result.worldSegments = pass.worldSegments;
      }
      if (shift < SETTLE_SECONDS)
      {
        result.settled = true;
        break;
      }

      if (shift >= previousShift)
      {
        stepScale = std::max(0.5f * stepScale, MIN_STEP_SCALE);
        previousX.clear();
      }
      else
      {
        stepScale = std::min(2.0f * stepScale, 1.0f);
      }

      double gamma = 0.0;
      if (!previousX.empty())
      {
        double dgDg = 0.0;
        double dgG = 0.0;
        for (size_t i = 1; i + 1 < count; i++)
        {
          const double dg = double(g[i]) - double(previousG[i]);
          dgDg += dg * dg;
          dgG += dg * double(g[i]);
        }
        if (dgDg > 1e-12)
          gamma = dgG / dgDg;
      }

      for (size_t i = 1; i + 1 < count; i++)
      {
        next[i] = x[i] + stepScale * g[i];
        if (gamma != 0.0)
        {
          const double dx = double(x[i]) - double(previousX[i]);
          const double dg = double(g[i]) - double(previousG[i]);
          next[i] -= float(gamma * (dx + stepScale * dg));
        }
      }
      keepOrder(next);

      previousX = x;
      previousG = g;
      previousShift = shift;
      x = next;
      for (size_t i = 1; i + 1 < count; i++)
        keys[first + i].time = x[i];
    }

    return result;
  }
}

#endif
