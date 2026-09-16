#pragma once

#include <glm/glm.hpp>

namespace YAEngine
{
  struct MotionSpeedKey
  {
    float time = 0.0f;
    // Metres per second. Never negative: a path is only ever driven forward.
    float speed = 0.0f;
  };

  // The curve through a path's control points, resampled at equal arc length, so a distance
  // along the path is a direct index
  struct MotionPathTable
  {
    // Inputs the table was built from; any difference means it is stale
    std::vector<glm::vec3> sourcePoints;
    float sourceSmoothing = -1.0f;

    float length = 0.0f;
    // Sample i sits at distance i * MOTION_PATH_STEP, except the last one, which sits at length
    std::vector<glm::vec3> positions;
    // Horizontal direction of travel as a yaw angle, atan2(forward.x, forward.z), unwrapped so
    // neighbours never differ by a full turn
    std::vector<float> headings;
    // d(yaw)/d(distance) in radians per metre, averaged along the path
    std::vector<float> curvature;
  };

  struct MotionPathTiming
  {
    float distance = 0.0f;
    float speed = 0.0f;
    float acceleration = 0.0f;
  };

  struct MotionPathPose
  {
    glm::vec3 position { 0.0f };
    // Unit and horizontal
    glm::vec3 forward { 0.0f, 0.0f, 1.0f };
    float yaw = 0.0f;
    float distance = 0.0f;
    float speed = 0.0f;
    float acceleration = 0.0f;
    float curvature = 0.0f;
  };

  inline constexpr float MOTION_PATH_STEP = 0.05f;

  namespace MotionPathDetail
  {
    // Dense sampling of the spline before the arc-length resample; fine enough that chord
    // length is arc length for any bend a car can take
    constexpr float DENSE_STEP = 0.02f;
    constexpr uint32_t MIN_DENSE_PER_SEGMENT = 8;
    constexpr uint32_t MAX_DENSE_PER_SEGMENT = 4096;
    constexpr float MIN_KNOT_GAP = 1e-4f;
    constexpr float PI = 3.14159265358979f;

    inline float WrapAngle(float angle)
    {
      while (angle > PI)
        angle -= 2.0f * PI;
      while (angle < -PI)
        angle += 2.0f * PI;
      return angle;
    }

    // Centripetal Catmull-Rom (alpha 0.5) in the Barry-Goldman form: interpolating, and it
    // never forms a cusp or a loop inside a segment, whatever the point spacing
    inline glm::vec3 CentripetalPoint(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2,
      const glm::vec3& p3, float u)
    {
      auto knot = [](const glm::vec3& a, const glm::vec3& b) {
        return std::sqrt(std::max(glm::length(b - a), MIN_KNOT_GAP));
      };

      float t0 = 0.0f;
      float t1 = t0 + knot(p0, p1);
      float t2 = t1 + knot(p1, p2);
      float t3 = t2 + knot(p2, p3);
      float t = glm::mix(t1, t2, u);

      glm::vec3 a1 = (t1 - t) / (t1 - t0) * p0 + (t - t0) / (t1 - t0) * p1;
      glm::vec3 a2 = (t2 - t) / (t2 - t1) * p1 + (t - t1) / (t2 - t1) * p2;
      glm::vec3 a3 = (t3 - t) / (t3 - t2) * p2 + (t - t2) / (t3 - t2) * p3;
      glm::vec3 b1 = (t2 - t) / (t2 - t0) * a1 + (t - t0) / (t2 - t0) * a2;
      glm::vec3 b2 = (t3 - t) / (t3 - t1) * a2 + (t - t1) / (t3 - t1) * a3;
      return (t2 - t) / (t2 - t1) * b1 + (t - t1) / (t2 - t1) * b2;
    }

    // Box average over +-radius samples; ends average over what exists
    inline void BoxFilter(std::vector<float>& values, int radius)
    {
      if (radius <= 0 || values.size() < 2)
        return;

      const int count = int(values.size());
      std::vector<double> prefix(values.size() + 1, 0.0);
      for (int i = 0; i < count; i++)
        prefix[i + 1] = prefix[i] + double(values[i]);

      for (int i = 0; i < count; i++)
      {
        int lo = std::max(0, i - radius);
        int hi = std::min(count - 1, i + radius);
        values[i] = float((prefix[hi + 1] - prefix[lo]) / double(hi - lo + 1));
      }
    }

    // Distance covered between two speed keys after u of the segment. Speed follows
    // v0 + (v1 - v0) * smoothstep(u), whose integral is closed form.
    inline float SegmentDistance(const MotionSpeedKey& k0, const MotionSpeedKey& k1, float u)
    {
      float span = k1.time - k0.time;
      float u3 = u * u * u;
      return span * (k0.speed * u + (k1.speed - k0.speed) * (u3 - 0.5f * u3 * u));
    }
  }

  inline bool IsMotionPathTableCurrent(const MotionPathTable& table, const std::vector<glm::vec3>& points,
    float smoothing)
  {
    return table.sourceSmoothing == smoothing && table.sourcePoints == points;
  }

  inline void BuildMotionPathTable(const std::vector<glm::vec3>& points, float smoothing, MotionPathTable& table)
  {
    using namespace MotionPathDetail;

    table.sourcePoints = points;
    table.sourceSmoothing = smoothing;
    table.length = 0.0f;
    table.positions.clear();
    table.headings.clear();
    table.curvature.clear();

    if (points.empty())
      return;

    if (points.size() == 1)
    {
      table.positions.push_back(points[0]);
      table.headings.push_back(0.0f);
      table.curvature.push_back(0.0f);
      return;
    }

    // Dense polyline with cumulative length
    std::vector<glm::vec3> dense;
    std::vector<float> denseDistance;
    dense.push_back(points[0]);
    denseDistance.push_back(0.0f);

    const size_t last = points.size() - 1;
    for (size_t seg = 0; seg < last; seg++)
    {
      const glm::vec3& p1 = points[seg];
      const glm::vec3& p2 = points[seg + 1];
      // Mirrored neighbours at the ends keep the first and last segments straight-ish
      glm::vec3 p0 = seg > 0 ? points[seg - 1] : 2.0f * p1 - p2;
      glm::vec3 p3 = seg + 2 <= last ? points[seg + 2] : 2.0f * p2 - p1;

      float chord = glm::length(p2 - p1);
      if (chord < MIN_KNOT_GAP)
        continue;

      uint32_t samples = uint32_t(std::ceil(chord / DENSE_STEP));
      samples = std::clamp(samples, MIN_DENSE_PER_SEGMENT, MAX_DENSE_PER_SEGMENT);
      for (uint32_t s = 1; s <= samples; s++)
      {
        glm::vec3 point = s == samples ? p2 : CentripetalPoint(p0, p1, p2, p3, float(s) / float(samples));
        denseDistance.push_back(denseDistance.back() + glm::length(point - dense.back()));
        dense.push_back(point);
      }
    }

    table.length = denseDistance.back();
    if (table.length < MIN_KNOT_GAP)
    {
      table.length = 0.0f;
      table.positions.push_back(points[0]);
      table.headings.push_back(0.0f);
      table.curvature.push_back(0.0f);
      return;
    }

    // Equal arc-length resample
    const size_t count = size_t(std::floor(table.length / MOTION_PATH_STEP)) + 2;
    table.positions.reserve(count);
    size_t cursor = 0;
    for (size_t i = 0; i < count; i++)
    {
      float distance = i + 1 == count ? table.length : std::min(float(i) * MOTION_PATH_STEP, table.length);
      while (cursor + 2 < dense.size() && denseDistance[cursor + 1] < distance)
        cursor++;
      float span = denseDistance[cursor + 1] - denseDistance[cursor];
      float f = span > 1e-8f ? (distance - denseDistance[cursor]) / span : 0.0f;
      table.positions.push_back(glm::mix(dense[cursor], dense[cursor + 1], glm::clamp(f, 0.0f, 1.0f)));
    }

    auto sampleDistance = [&table, count](size_t i) {
      return i + 1 == count ? table.length : float(i) * MOTION_PATH_STEP;
    };

    // Heading from central differences, unwrapped against the previous sample
    table.headings.resize(count);
    for (size_t i = 0; i < count; i++)
    {
      size_t a = i > 0 ? i - 1 : 0;
      size_t b = std::min(i + 1, count - 1);
      glm::vec3 d = table.positions[b] - table.positions[a];
      float heading = (std::abs(d.x) + std::abs(d.z) > 1e-8f) ? std::atan2(d.x, d.z)
        : (i > 0 ? table.headings[i - 1] : 0.0f);
      if (i > 0)
        heading = table.headings[i - 1] + WrapAngle(heading - table.headings[i - 1]);
      table.headings[i] = heading;
    }

    table.curvature.resize(count);
    for (size_t i = 0; i < count; i++)
    {
      size_t a = i > 0 ? i - 1 : 0;
      size_t b = std::min(i + 1, count - 1);
      float span = sampleDistance(b) - sampleDistance(a);
      table.curvature[i] = span > 1e-6f ? (table.headings[b] - table.headings[a]) / span : 0.0f;
    }

    // Catmull-Rom is only C1: its curvature jumps at every control point, which would snap the
    // steering. Two box passes make a triangle window of the requested width.
    int radius = int(std::round(std::max(smoothing, 0.0f) * 0.25f / MOTION_PATH_STEP));
    BoxFilter(table.curvature, radius);
    BoxFilter(table.curvature, radius);
  }

  // Everything the table knows at a distance along the path, interpolated between samples
  inline MotionPathPose SampleMotionPath(const MotionPathTable& table, float distance)
  {
    MotionPathPose pose;
    if (table.positions.empty())
      return pose;

    if (table.positions.size() == 1)
    {
      pose.position = table.positions[0];
      return pose;
    }

    const size_t count = table.positions.size();
    distance = glm::clamp(distance, 0.0f, table.length);

    size_t i = std::min(size_t(distance / MOTION_PATH_STEP), count - 2);
    float start = float(i) * MOTION_PATH_STEP;
    float end = i + 2 == count ? table.length : float(i + 1) * MOTION_PATH_STEP;
    float f = end - start > 1e-8f ? glm::clamp((distance - start) / (end - start), 0.0f, 1.0f) : 0.0f;

    pose.position = glm::mix(table.positions[i], table.positions[i + 1], f);
    pose.yaw = glm::mix(table.headings[i], table.headings[i + 1], f);
    pose.forward = glm::vec3(std::sin(pose.yaw), 0.0f, std::cos(pose.yaw));
    pose.curvature = glm::mix(table.curvature[i], table.curvature[i + 1], f);
    pose.distance = distance;
    return pose;
  }

  // Keys must be sorted by time. Before the first key the path is parked at its start; after
  // the last one the last speed holds.
  inline MotionPathTiming EvaluateMotionTiming(const std::vector<MotionSpeedKey>& keys, float time)
  {
    MotionPathTiming timing;
    if (keys.empty() || time <= keys.front().time)
      return timing;

    float distance = 0.0f;
    for (size_t i = 0; i + 1 < keys.size(); i++)
    {
      const MotionSpeedKey& k0 = keys[i];
      const MotionSpeedKey& k1 = keys[i + 1];
      float span = k1.time - k0.time;
      if (time < k1.time && span > 1e-6f)
      {
        float u = (time - k0.time) / span;
        float dv = k1.speed - k0.speed;
        timing.distance = distance + MotionPathDetail::SegmentDistance(k0, k1, u);
        timing.speed = k0.speed + dv * u * u * (3.0f - 2.0f * u);
        timing.acceleration = dv * 6.0f * u * (1.0f - u) / span;
        return timing;
      }
      distance += MotionPathDetail::SegmentDistance(k0, k1, 1.0f);
    }

    const MotionSpeedKey& lastKey = keys.back();
    timing.distance = distance + lastKey.speed * (time - lastKey.time);
    timing.speed = lastKey.speed;
    return timing;
  }

  // First time the timing covers the given distance; infinity if it never does
  inline float MotionTimeAtDistance(const std::vector<MotionSpeedKey>& keys, float target)
  {
    if (keys.empty())
      return std::numeric_limits<float>::infinity();
    if (target <= 0.0f)
      return keys.front().time;

    float distance = 0.0f;
    for (size_t i = 0; i + 1 < keys.size(); i++)
    {
      const MotionSpeedKey& k0 = keys[i];
      const MotionSpeedKey& k1 = keys[i + 1];
      float segment = MotionPathDetail::SegmentDistance(k0, k1, 1.0f);
      if (distance + segment >= target && segment > 0.0f)
      {
        // Speed is never negative, so distance grows monotonically inside the segment
        float lo = 0.0f;
        float hi = 1.0f;
        for (int iteration = 0; iteration < 40; iteration++)
        {
          float mid = 0.5f * (lo + hi);
          if (distance + MotionPathDetail::SegmentDistance(k0, k1, mid) < target)
            lo = mid;
          else
            hi = mid;
        }
        return glm::mix(k0.time, k1.time, hi);
      }
      distance += segment;
    }

    const MotionSpeedKey& lastKey = keys.back();
    if (lastKey.speed <= 0.0f)
      return std::numeric_limits<float>::infinity();
    return lastKey.time + (target - distance) / lastKey.speed;
  }

  // Time at which the drive is over: the end of the path, or the last key if the path is
  // never finished
  inline float MotionPathDuration(const MotionPathTable& table, const std::vector<MotionSpeedKey>& keys)
  {
    if (keys.empty())
      return 0.0f;
    float end = MotionTimeAtDistance(keys, table.length);
    return std::isfinite(end) ? std::max(end, keys.back().time) : keys.back().time;
  }

  // Pure function of time: the same time gives the same pose at any frame rate and in any order
  inline MotionPathPose EvaluateMotionPath(const MotionPathTable& table, const std::vector<MotionSpeedKey>& keys,
    float time)
  {
    MotionPathTiming timing = EvaluateMotionTiming(keys, time);
    MotionPathPose pose = SampleMotionPath(table, timing.distance);
    // The path ran out: the drive stops at its end
    if (timing.distance < table.length)
    {
      pose.speed = timing.speed;
      pose.acceleration = timing.acceleration;
    }
    return pose;
  }
}
