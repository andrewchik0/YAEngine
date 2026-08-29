#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace YAEngine
{
  struct CameraTrackKey
  {
    float time = 0.0f;
    glm::vec3 position { 0.0f };
    glm::quat rotation { 1, 0, 0, 0 };
    float fov = glm::radians(58.31f);
  };

  struct CameraTrackPose
  {
    glm::vec3 position { 0.0f };
    glm::quat rotation { 1, 0, 0, 0 };
    float fov = glm::radians(58.31f);
  };

  namespace CameraTrackDetail
  {
    // Velocity at a key, in units per second, so unequal key spacing does not turn into
    // a speed spike: the two neighbouring secants are averaged weighted by the OPPOSITE
    // interval, which is what keeps the tangent identical seen from either segment (C1).
    // Endpoints have one neighbour only, so they take that secant unchanged.
    template <typename Value>
    inline auto KeyTangent(const std::vector<CameraTrackKey>& keys, size_t index, Value value)
      -> decltype(value(keys[0]))
    {
      using T = decltype(value(keys[0]));
      const size_t last = keys.size() - 1;

      auto secant = [&keys, &value](size_t a, size_t b) -> T
      {
        float span = keys[b].time - keys[a].time;
        if (span <= 1e-6f)
          return T(0.0f);
        return (value(keys[b]) - value(keys[a])) / span;
      };

      if (index == 0)
        return secant(0, 1);
      if (index == last)
        return secant(last - 1, last);

      float prevSpan = keys[index].time - keys[index - 1].time;
      float nextSpan = keys[index + 1].time - keys[index].time;
      float total = prevSpan + nextSpan;
      if (total <= 1e-6f)
        return T(0.0f);

      return (secant(index - 1, index) * nextSpan + secant(index, index + 1) * prevSpan) / total;
    }

    // Rotation-vector log/exp of a unit quaternion, kept local so the header stays on
    // plain glm scalar math
    inline glm::vec3 QuatLog(const glm::quat& q)
    {
      glm::vec3 v(q.x, q.y, q.z);
      float vLen = glm::length(v);
      if (vLen <= 1e-8f)
        return glm::vec3(0.0f);
      float angle = 2.0f * glm::atan(vLen, q.w);
      return v * (angle / vLen);
    }

    inline glm::quat QuatExp(const glm::vec3& v)
    {
      float angle = glm::length(v);
      if (angle <= 1e-8f)
        return glm::normalize(glm::quat(1.0f, 0.5f * v.x, 0.5f * v.y, 0.5f * v.z));
      glm::vec3 axis = v / angle;
      float half = 0.5f * angle;
      return glm::quat(glm::cos(half), axis * glm::sin(half));
    }

    // Shortest-arc rotation from a to b as a rotation vector in a's local frame
    inline glm::vec3 RelativeLog(const glm::quat& a, glm::quat b)
    {
      if (glm::dot(a, b) < 0.0f)
        b = -b;
      return QuatLog(glm::normalize(glm::inverse(a) * b));
    }

    // Angular velocity at a key (rotation vector per second), weighted like KeyTangent.
    // The two rates live in slightly different local frames (q[i-1] vs q[i]); mixing
    // them is the usual squad-style small-angle approximation and is what keeps the
    // per-key rotation kink of pairwise slerp from being visible.
    inline glm::vec3 KeyAngularVelocity(const std::vector<CameraTrackKey>& keys, size_t index)
    {
      const size_t last = keys.size() - 1;

      auto rate = [&keys](size_t a, size_t b) -> glm::vec3
      {
        float span = keys[b].time - keys[a].time;
        if (span <= 1e-6f)
          return glm::vec3(0.0f);
        return RelativeLog(keys[a].rotation, keys[b].rotation) / span;
      };

      if (index == 0)
        return rate(0, 1);
      if (index == last)
        return rate(last - 1, last);

      float prevSpan = keys[index].time - keys[index - 1].time;
      float nextSpan = keys[index + 1].time - keys[index].time;
      float total = prevSpan + nextSpan;
      if (total <= 1e-6f)
        return glm::vec3(0.0f);

      return (rate(index - 1, index) * nextSpan + rate(index, index + 1) * prevSpan) / total;
    }
  }

  // Keys must be sorted by time. Position and fov run through a cubic Hermite with
  // non-uniform Catmull-Rom tangents (interpolating and C1). Rotation is the same
  // Hermite in the log space of the segment's relative rotation, so angular velocity is
  // continuous across keys too; with two keys it degenerates to an exact slerp. AimAt
  // rotation is the caller's business - this stays free of the scene.
  inline CameraTrackPose EvaluateCameraTrack(const std::vector<CameraTrackKey>& keys, float t)
  {
    CameraTrackPose pose;
    if (keys.empty())
      return pose;

    if (keys.size() == 1)
    {
      pose.position = keys[0].position;
      pose.rotation = keys[0].rotation;
      pose.fov = keys[0].fov;
      return pose;
    }

    t = glm::clamp(t, keys.front().time, keys.back().time);

    size_t seg = 0;
    while (seg + 2 < keys.size() && keys[seg + 1].time <= t)
      seg++;

    const CameraTrackKey& k0 = keys[seg];
    const CameraTrackKey& k1 = keys[seg + 1];

    float span = k1.time - k0.time;
    float u = (span > 1e-6f) ? (t - k0.time) / span : 0.0f;

    auto position = [](const CameraTrackKey& k) { return k.position; };
    auto fov = [](const CameraTrackKey& k) { return k.fov; };
    glm::vec3 m0 = CameraTrackDetail::KeyTangent(keys, seg, position);
    glm::vec3 m1 = CameraTrackDetail::KeyTangent(keys, seg + 1, position);
    float f0 = CameraTrackDetail::KeyTangent(keys, seg, fov);
    float f1 = CameraTrackDetail::KeyTangent(keys, seg + 1, fov);

    float u2 = u * u;
    float u3 = u2 * u;
    float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
    float h10 = u3 - 2.0f * u2 + u;
    float h01 = -2.0f * u3 + 3.0f * u2;
    float h11 = u3 - u2;

    // Tangents are per second, the Hermite basis is per unit of u, hence the span factor
    pose.position = h00 * k0.position + h10 * span * m0
                  + h01 * k1.position + h11 * span * m1;
    pose.fov = h00 * k0.fov + h10 * span * f0
             + h01 * k1.fov + h11 * span * f1;

    // Same Hermite, in the log space of the relative rotation: H runs from zero to the
    // full segment rotation with the blended angular velocities at the ends (h00 is
    // absent because the curve starts at zero)
    glm::vec3 w0 = CameraTrackDetail::KeyAngularVelocity(keys, seg);
    glm::vec3 w1 = CameraTrackDetail::KeyAngularVelocity(keys, seg + 1);
    glm::vec3 r = CameraTrackDetail::RelativeLog(k0.rotation, k1.rotation);
    glm::vec3 H = h10 * span * w0 + h01 * r + h11 * span * w1;
    pose.rotation = glm::normalize(k0.rotation * CameraTrackDetail::QuatExp(H));

    return pose;
  }
}
