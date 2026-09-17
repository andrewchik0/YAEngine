#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace YAEngine
{
  enum class CameraKeySpace : uint8_t
  {
    World,
    // Position and rotation are relative to the track's follow target
    Target
  };

  // How the segment after a key interpolates
  enum class CameraKeyInterp : uint8_t
  {
    Smooth,
    Linear,
    Hold,
    // Around the follow target's up axis; needs Target keys at both ends, otherwise Smooth
    Orbit
  };

  struct CameraTrackKey
  {
    float time = 0.0f;
    glm::vec3 position { 0.0f };
    glm::quat rotation { 1, 0, 0, 0 };
    float fov = glm::radians(58.31f);
    CameraKeySpace space = CameraKeySpace::World;
    CameraKeyInterp interpOut = CameraKeyInterp::Smooth;
    // Scale of the key's tangent on the arriving and the leaving side: 0 brings the camera to
    // rest at the key, 1 keeps the Catmull-Rom tangent
    float easeIn = 1.0f;
    float easeOut = 1.0f;
  };

  struct CameraTrackPose
  {
    glm::vec3 position { 0.0f };
    glm::quat rotation { 1, 0, 0, 0 };
    float fov = glm::radians(58.31f);
  };

  // World transform of the follow target at the evaluated time
  struct CameraTargetFrame
  {
    glm::vec3 position { 0.0f };
    glm::quat rotation { 1, 0, 0, 0 };
  };

  namespace CameraTrackDetail
  {
    inline constexpr float TWO_PI = 6.28318530717958648f;

    // Rate of change at a key from the per-second rates of its two neighbouring segments, so
    // unequal key spacing does not turn into a speed spike: the rates are averaged weighted by
    // the OPPOSITE interval, which is what keeps the tangent identical seen from either
    // segment (C1). Endpoints have one neighbour only, so they take that rate unchanged.
    template <typename Rate>
    inline auto WeightedKeyRate(const std::vector<CameraTrackKey>& keys, size_t index, Rate rate)
      -> decltype(rate(size_t(0), size_t(1)))
    {
      using T = decltype(rate(size_t(0), size_t(1)));
      const size_t last = keys.size() - 1;

      if (index == 0)
        return rate(0, 1);
      if (index == last)
        return rate(last - 1, last);

      float prevSpan = keys[index].time - keys[index - 1].time;
      float nextSpan = keys[index + 1].time - keys[index].time;
      float total = prevSpan + nextSpan;
      if (total <= 1e-6f)
        return T(0.0f);

      return (rate(index - 1, index) * nextSpan + rate(index, index + 1) * prevSpan) / total;
    }

    // Velocity at a key, in units per second, of whatever the accessor reads
    template <typename Value>
    inline auto KeyTangent(const std::vector<CameraTrackKey>& keys, size_t index, Value value)
      -> decltype(value(keys[0]))
    {
      using T = decltype(value(keys[0]));
      return WeightedKeyRate(keys, index, [&keys, &value](size_t a, size_t b) -> T
      {
        float span = keys[b].time - keys[a].time;
        if (span <= 1e-6f)
          return T(0.0f);
        return (value(keys[b]) - value(keys[a])) / span;
      });
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

    // Angular velocity at a key (rotation vector per second) of the rotation the accessor
    // reads, weighted like KeyTangent. The two rates live in slightly different local frames
    // (q[i-1] vs q[i]); mixing them is the usual squad-style small-angle approximation and is
    // what keeps the per-key rotation kink of pairwise slerp from being visible.
    template <typename Rotation>
    inline glm::vec3 KeyAngularVelocity(const std::vector<CameraTrackKey>& keys, size_t index, Rotation rotation)
    {
      return WeightedKeyRate(keys, index, [&keys, &rotation](size_t a, size_t b) -> glm::vec3
      {
        float span = keys[b].time - keys[a].time;
        if (span <= 1e-6f)
          return glm::vec3(0.0f);
        return RelativeLog(rotation(keys[a]), rotation(keys[b])) / span;
      });
    }

    inline float WrapAngle(float angle)
    {
      return std::remainder(angle, TWO_PI);
    }

    inline bool IsTargetKey(const CameraTrackKey& key, const CameraTargetFrame* frame)
    {
      return frame != nullptr && key.space == CameraKeySpace::Target;
    }

    // Whether the segment from keys[index] to keys[index + 1] orbits
    inline bool IsOrbitSegment(const std::vector<CameraTrackKey>& keys, size_t index, const CameraTargetFrame* frame)
    {
      return index + 1 < keys.size() && keys[index].interpOut == CameraKeyInterp::Orbit
        && IsTargetKey(keys[index], frame) && IsTargetKey(keys[index + 1], frame);
    }

    // The space a segment is interpolated in. World reads Target keys through the frame;
    // local reads World keys through the inverse frame and leaves Target keys as authored.
    // The frame is rigid and every blend is affine, so both give the same pose up to
    // rounding - local exists because an orbit is defined around the frame origin.
    struct KeySpace
    {
      const CameraTargetFrame* frame = nullptr;
      bool local = false;

      bool IsAuthoredSpace(const CameraTrackKey& key) const
      {
        return frame == nullptr || (key.space == CameraKeySpace::Target) == local;
      }

      glm::vec3 Position(const CameraTrackKey& key) const
      {
        if (IsAuthoredSpace(key))
          return key.position;
        if (local)
          return glm::inverse(frame->rotation) * (key.position - frame->position);
        return frame->rotation * key.position + frame->position;
      }

      glm::quat Rotation(const CameraTrackKey& key) const
      {
        if (IsAuthoredSpace(key))
          return key.rotation;
        if (local)
          return glm::normalize(glm::inverse(frame->rotation) * key.rotation);
        return glm::normalize(frame->rotation * key.rotation);
      }

      void ToWorld(CameraTrackPose& pose) const
      {
        if (frame == nullptr || !local)
          return;
        pose.position = frame->rotation * pose.position + frame->position;
        pose.rotation = glm::normalize(frame->rotation * pose.rotation);
      }
    };

    // Angle around the up axis (the heading convention, atan2(x, z)), radius, height
    inline glm::vec3 ToCylindrical(const glm::vec3& p)
    {
      return glm::vec3(std::atan2(p.x, p.z), glm::length(glm::vec2(p.x, p.z)), p.y);
    }

    inline glm::vec3 FromCylindrical(const glm::vec3& c)
    {
      return glm::vec3(c.y * std::sin(c.x), c.z, c.y * std::cos(c.x));
    }

    inline glm::quat YawRotation(float angle)
    {
      return glm::angleAxis(angle, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    // d(angle, radius, height)/dt at a key, the angle always taking the shortest turn
    inline glm::vec3 OrbitRate(const std::vector<CameraTrackKey>& keys, size_t index, const KeySpace& space)
    {
      auto rate = [&keys, &space](size_t a, size_t b) -> glm::vec3
      {
        float span = keys[b].time - keys[a].time;
        if (span <= 1e-6f)
          return glm::vec3(0.0f);
        glm::vec3 delta = ToCylindrical(space.Position(keys[b])) - ToCylindrical(space.Position(keys[a]));
        delta.x = WrapAngle(delta.x);
        return delta / span;
      };

      glm::vec3 result = WeightedKeyRate(keys, index, rate);

      // A radius overshooting its keys brings the camera closer to the target than any key
      // asked for, into the car at worst. Fritsch-Carlson limiting keeps each segment's
      // radius monotone: flat at a radius extremum, at most three secants steep elsewhere.
      if (index > 0 && index + 1 < keys.size())
      {
        float before = rate(index - 1, index).y;
        float after = rate(index, index + 1).y;
        float limit = 3.0f * std::min(std::abs(before), std::abs(after));
        result.y = before * after <= 0.0f ? 0.0f : glm::clamp(result.y, -limit, limit);
      }
      return result;
    }

    // Rotation with the orbit angle taken out. Interpolating that instead of the plain
    // rotation keeps a camera that faces the target at both ends facing it all the way
    // round; the plain shortest arc may turn the other way than the orbit goes.
    inline glm::quat OrbitRelativeRotation(const CameraTrackKey& key, const KeySpace& space)
    {
      glm::vec3 p = space.Position(key);
      return glm::normalize(glm::inverse(YawRotation(std::atan2(p.x, p.z))) * space.Rotation(key));
    }

    inline glm::vec3 OrbitRelativeAngularVelocity(const std::vector<CameraTrackKey>& keys, size_t index,
      const KeySpace& space)
    {
      return KeyAngularVelocity(keys, index,
        [&space](const CameraTrackKey& key) { return OrbitRelativeRotation(key, space); });
    }

    inline bool TouchesOrbit(const std::vector<CameraTrackKey>& keys, size_t index, const CameraTargetFrame* frame)
    {
      return (index > 0 && IsOrbitSegment(keys, index - 1, frame)) || IsOrbitSegment(keys, index, frame);
    }

    // At a key shared with an orbit the non-orbit segment takes the orbit's own velocity,
    // so entering or leaving an orbit has no kink. Callers guarantee a local space there.
    inline glm::vec3 KeyVelocity(const std::vector<CameraTrackKey>& keys, size_t index,
      const CameraTargetFrame* frame, const KeySpace& space)
    {
      if (!TouchesOrbit(keys, index, frame))
        return KeyTangent(keys, index, [&space](const CameraTrackKey& key) { return space.Position(key); });

      glm::vec3 c = ToCylindrical(space.Position(keys[index]));
      glm::vec3 rate = OrbitRate(keys, index, space);
      float s = std::sin(c.x);
      float co = std::cos(c.x);
      return glm::vec3(rate.y * s + c.y * co * rate.x, rate.z, rate.y * co - c.y * s * rate.x);
    }

    inline glm::vec3 KeyBodyAngularVelocity(const std::vector<CameraTrackKey>& keys, size_t index,
      const CameraTargetFrame* frame, const KeySpace& space)
    {
      if (!TouchesOrbit(keys, index, frame))
        return KeyAngularVelocity(keys, index, [&space](const CameraTrackKey& key) { return space.Rotation(key); });

      // q = yaw(angle) * relative: the yaw rate seen from the camera plus the relative rate
      glm::quat relative = OrbitRelativeRotation(keys[index], space);
      float angleRate = OrbitRate(keys, index, space).x;
      return glm::inverse(relative) * glm::vec3(0.0f, angleRate, 0.0f)
        + OrbitRelativeAngularVelocity(keys, index, space);
    }
  }

  // Keys must be sorted by time. Smooth runs position and fov through a cubic Hermite with
  // non-uniform Catmull-Rom tangents (interpolating and C1). Rotation is the same Hermite in
  // the log space of the segment's relative rotation, so angular velocity is continuous
  // across keys too; with two keys it degenerates to an exact slerp. A key's ease scales its
  // tangents, and its interpOut picks how the segment after it moves (see CameraKeyInterp).
  //
  // frame is the follow target at time t; without one, Target keys are read as World. A
  // segment whose keys and tangent neighbours are all Target keys is interpolated in the
  // target's space and moves with it; otherwise the Target keys are placed in the world
  // through the frame first. AimAt rotation is the caller's business - this stays free of
  // the scene.
  inline CameraTrackPose EvaluateCameraTrack(const std::vector<CameraTrackKey>& keys, float t,
    const CameraTargetFrame* frame)
  {
    using namespace CameraTrackDetail;

    CameraTrackPose pose;
    if (keys.empty())
      return pose;

    if (keys.size() == 1)
    {
      const KeySpace space { .frame = frame };
      pose.position = space.Position(keys[0]);
      pose.rotation = space.Rotation(keys[0]);
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

    // Tangents read one key either side of the segment
    const size_t first = seg > 0 ? seg - 1 : 0;
    const size_t last = std::min(seg + 2, keys.size() - 1);
    bool allTarget = frame != nullptr;
    for (size_t i = first; i <= last; i++)
      allTarget = allTarget && keys[i].space == CameraKeySpace::Target;
    const bool nearOrbit = TouchesOrbit(keys, seg, frame) || TouchesOrbit(keys, seg + 1, frame);
    const KeySpace space { .frame = frame, .local = allTarget || nearOrbit };

    const glm::vec3 p0 = space.Position(k0);
    const glm::vec3 p1 = space.Position(k1);
    const glm::quat q0 = space.Rotation(k0);
    const glm::quat q1 = space.Rotation(k1);

    CameraKeyInterp interp = k0.interpOut;
    if (interp == CameraKeyInterp::Orbit && !IsOrbitSegment(keys, seg, frame))
      interp = CameraKeyInterp::Smooth;

    if (interp == CameraKeyInterp::Hold)
    {
      // u only reaches 1 at the very last key; any other key already starts the next segment
      const bool arrived = u >= 1.0f;
      pose.position = arrived ? p1 : p0;
      pose.rotation = arrived ? q1 : q0;
      pose.fov = arrived ? k1.fov : k0.fov;
      space.ToWorld(pose);
      return pose;
    }

    float u2 = u * u;
    float u3 = u2 * u;
    float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
    float h10 = u3 - 2.0f * u2 + u;
    float h01 = -2.0f * u3 + 3.0f * u2;
    float h11 = u3 - u2;

    if (interp == CameraKeyInterp::Linear)
    {
      // The eases reshape the pace along the line: a Hermite from 0 to 1 whose end slopes are
      // the eases, which is u itself when both are 1
      float s = (k0.easeOut == 1.0f && k1.easeIn == 1.0f) ? u
        : h10 * k0.easeOut + h01 + h11 * k1.easeIn;
      pose.position = glm::mix(p0, p1, s);
      pose.rotation = glm::normalize(q0 * QuatExp(s * RelativeLog(q0, q1)));
      pose.fov = glm::mix(k0.fov, k1.fov, s);
      space.ToWorld(pose);
      return pose;
    }

    auto fov = [](const CameraTrackKey& k) { return k.fov; };
    float f0 = KeyTangent(keys, seg, fov) * k0.easeOut;
    float f1 = KeyTangent(keys, seg + 1, fov) * k1.easeIn;
    // Tangents are per second, the Hermite basis is per unit of u, hence the span factor
    pose.fov = h00 * k0.fov + h10 * span * f0
             + h01 * k1.fov + h11 * span * f1;

    if (interp == CameraKeyInterp::Orbit)
    {
      glm::vec3 c0 = ToCylindrical(p0);
      glm::vec3 c1 = ToCylindrical(p1);
      c1.x = c0.x + WrapAngle(c1.x - c0.x);
      glm::vec3 m0 = OrbitRate(keys, seg, space) * k0.easeOut;
      glm::vec3 m1 = OrbitRate(keys, seg + 1, space) * k1.easeIn;
      glm::vec3 c = h00 * c0 + h10 * span * m0
                  + h01 * c1 + h11 * span * m1;
      c.y = std::max(c.y, 0.0f);
      pose.position = FromCylindrical(c);

      glm::quat r0 = glm::normalize(glm::inverse(YawRotation(c0.x)) * q0);
      glm::quat r1 = glm::normalize(glm::inverse(YawRotation(c1.x)) * q1);
      glm::vec3 w0 = OrbitRelativeAngularVelocity(keys, seg, space) * k0.easeOut;
      glm::vec3 w1 = OrbitRelativeAngularVelocity(keys, seg + 1, space) * k1.easeIn;
      glm::vec3 H = h10 * span * w0 + h01 * RelativeLog(r0, r1) + h11 * span * w1;
      pose.rotation = glm::normalize(YawRotation(c.x) * r0 * QuatExp(H));

      space.ToWorld(pose);
      return pose;
    }

    glm::vec3 m0 = KeyVelocity(keys, seg, frame, space) * k0.easeOut;
    glm::vec3 m1 = KeyVelocity(keys, seg + 1, frame, space) * k1.easeIn;
    pose.position = h00 * p0 + h10 * span * m0
                  + h01 * p1 + h11 * span * m1;

    // Same Hermite, in the log space of the relative rotation: H runs from zero to the
    // full segment rotation with the blended angular velocities at the ends (h00 is
    // absent because the curve starts at zero)
    glm::vec3 w0 = KeyBodyAngularVelocity(keys, seg, frame, space) * k0.easeOut;
    glm::vec3 w1 = KeyBodyAngularVelocity(keys, seg + 1, frame, space) * k1.easeIn;
    glm::vec3 r = RelativeLog(q0, q1);
    glm::vec3 H = h10 * span * w0 + h01 * r + h11 * span * w1;
    pose.rotation = glm::normalize(q0 * QuatExp(H));

    space.ToWorld(pose);
    return pose;
  }

  // World keys only: Target keys are read as World
  inline CameraTrackPose EvaluateCameraTrack(const std::vector<CameraTrackKey>& keys, float t)
  {
    return EvaluateCameraTrack(keys, t, nullptr);
  }
}
