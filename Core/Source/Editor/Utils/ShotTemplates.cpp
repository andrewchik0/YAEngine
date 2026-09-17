#ifdef YA_EDITOR

#include "Editor/Utils/ShotTemplates.h"

namespace YAEngine::ShotTemplates
{
  namespace
  {
    constexpr int32_t MAX_ORBIT_SEGMENTS = 3;
    // The orbit interpolates the angle around the frame origin along the shortest turn; a step near half a
    // turn could flip to the other way round, straight through the target
    constexpr float MAX_ORBIT_STEP = glm::radians(150.0f);
    constexpr float MIN_ORBIT_RADIUS = 0.5f;
    constexpr float MIN_FOV_DEGREES = 1.0f;
    constexpr float MAX_FOV_DEGREES = 170.0f;
    constexpr float PI = glm::pi<float>();

    // Same basis as the SequencePlayer's aim: -Z forward, +Y up, no roll
    glm::quat LookAt(const glm::vec3& from, const glm::vec3& target)
    {
      glm::vec3 forward = target - from;
      float length = glm::length(forward);
      if (length <= 1e-5f)
        return glm::quat(1, 0, 0, 0);

      forward /= length;

      glm::vec3 up(0.0f, 1.0f, 0.0f);
      if (std::abs(forward.y) > 0.999f)
        up = glm::vec3(0.0f, 0.0f, 1.0f);

      glm::vec3 right = glm::normalize(glm::cross(forward, up));
      glm::vec3 trueUp = glm::cross(right, forward);
      return glm::normalize(glm::quat_cast(glm::mat3(right, trueUp, -forward)));
    }

    CameraTrackKey MakeKey(float time, CameraKeySpace space, const glm::vec3& position, const glm::quat& rotation,
      float fov)
    {
      CameraTrackKey key;
      key.time = time;
      key.position = position;
      key.rotation = glm::normalize(rotation);
      key.fov = fov;
      key.space = space;
      return key;
    }

    CameraTrackKey TargetKey(float time, const glm::vec3& position, const glm::vec3& aim, float fov)
    {
      return MakeKey(time, CameraKeySpace::Target, position, LookAt(position, aim), fov);
    }

    // A point at a bearing and horizontal distance from the body centre, at an absolute height
    glm::vec3 AroundBody(const ShotSubject& subject, float degrees, float distance, float height)
    {
      float angle = glm::radians(degrees);
      glm::vec3 point = subject.bodyCentre + glm::vec3(std::sin(angle), 0.0f, std::cos(angle)) * distance;
      point.y = height;
      return point;
    }

    float SideSign(ShotSide side)
    {
      return side == ShotSide::Left ? 1.0f : -1.0f;
    }

    // The camera comes to rest at both ends of the shot, also against keys of a neighbouring shot
    void EaseEnds(std::vector<CameraTrackKey>& keys)
    {
      keys.front().easeIn = 0.0f;
      keys.front().easeOut = 0.0f;
      keys.back().easeIn = 0.0f;
      keys.back().easeOut = 0.0f;
    }

    float WrapAngle(float angle)
    {
      angle = std::fmod(angle + PI, 2.0f * PI);
      if (angle < 0.0f)
        angle += 2.0f * PI;
      return angle - PI;
    }

    // Keys are placed around the body centre but interpolated around the frame origin: every step has to
    // turn the same way round the origin, and far enough that a full turn does not collapse to none
    bool OrbitTurnsAgree(const std::vector<CameraTrackKey>& keys, float span)
    {
      const float step = span / float(keys.size() - 1);
      if (std::abs(step) > MAX_ORBIT_STEP)
        return false;

      for (size_t i = 0; i + 1 < keys.size(); i++)
      {
        const glm::vec3& a = keys[i].position;
        const glm::vec3& b = keys[i + 1].position;
        if (glm::length(glm::vec2(a.x, a.z)) < MIN_ORBIT_RADIUS || glm::length(glm::vec2(b.x, b.z)) < MIN_ORBIT_RADIUS)
          return false;

        float turn = WrapAngle(std::atan2(b.x, b.z) - std::atan2(a.x, a.z));
        if (std::abs(turn) > MAX_ORBIT_STEP)
          return false;
        if (turn * step < 0.0f || std::abs(turn) < 0.25f * std::abs(step))
          return false;
      }
      return true;
    }

    const char* Orbit(const ShotTemplateParams& p, float fov, std::vector<CameraTrackKey>& keys)
    {
      const float span = glm::radians(p.orbitEndDegrees - p.orbitStartDegrees);
      for (int32_t segments = 1; segments <= MAX_ORBIT_SEGMENTS; segments++)
      {
        keys.clear();
        for (int32_t i = 0; i <= segments; i++)
        {
          float f = float(i) / float(segments);
          float degrees = glm::mix(p.orbitStartDegrees, p.orbitEndDegrees, f);
          glm::vec3 position = AroundBody(p.subject, degrees, p.distance, p.height);
          CameraTrackKey key = TargetKey(p.start + f * p.duration, position, p.subject.bodyCentre, fov);
          key.interpOut = i < segments ? CameraKeyInterp::Orbit : CameraKeyInterp::Smooth;
          keys.push_back(key);
        }

        if (OrbitTurnsAgree(keys, span))
          return nullptr;
      }

      keys.clear();
      return "The orbit turns too far or passes too close to the target frame origin for one shot: "
             "narrow the angles, widen the radius or split it into two shots";
    }

    ShotTemplateParams Defaults(float duration, float distance, float endDistance, float height, float fovDegrees)
    {
      ShotTemplateParams params;
      params.duration = duration;
      params.distance = distance;
      params.endDistance = endDistance;
      params.height = height;
      params.fovDegrees = fovDegrees;
      return params;
    }

    std::array<ShotTemplateInfo, size_t(ShotTemplate::Count)> BuildInfos()
    {
      using namespace ShotParam;
      std::array<ShotTemplateInfo, size_t(ShotTemplate::Count)> infos;

      infos[size_t(ShotTemplate::Chase)] = {
        .name = "Chase",
        .tooltip = "Behind and above the target, easing a little closer, aimed ahead of it. Rides on the target "
                   "with its heading smoothed.",
        .defaults = Defaults(5.0f, 10.0f, 9.0f, 2.3f, 50.0f),
        .params = Distance | EndDistance | Height | Fov | LookAhead,
      };

      infos[size_t(ShotTemplate::Lead)] = {
        .name = "Lead",
        .tooltip = "Low in front of the target, looking back at it while it closes in a little. Rides on the "
                   "target with its heading smoothed.",
        .defaults = Defaults(5.0f, 8.0f, 7.0f, 1.4f, 45.0f),
        .params = Distance | EndDistance | Height | Fov,
      };

      ShotTemplateParams side = Defaults(5.0f, 5.5f, 5.5f, 1.5f, 45.0f);
      side.drift = 2.0f;
      infos[size_t(ShotTemplate::SideTracking)] = {
        .name = "Side Tracking",
        .tooltip = "Alongside the target, slowly drifting forward along it and keeping it framed. Rides on the "
                   "target with its heading smoothed.",
        .defaults = side,
        .params = Distance | Height | Side | Fov | Drift,
      };

      ShotTemplateParams wheel = Defaults(3.0f, 2.4f, 2.4f, 0.95f, 40.0f);
      wheel.drift = -0.6f;
      wheel.lateral = 1.3f;
      infos[size_t(ShotTemplate::WheelCloseUpLeft)] = {
        .name = "Wheel Close-Up Left",
        .tooltip = "Low, just ahead of and outside the left front wheel, looking at it. Distance is measured "
                   "ahead of the wheel. Rides rigidly on the target.",
        .defaults = wheel,
        .params = Distance | Height | Fov | Drift | Lateral,
      };

      wheel.side = ShotSide::Right;
      infos[size_t(ShotTemplate::WheelCloseUpRight)] = {
        .name = "Wheel Close-Up Right",
        .tooltip = "Low, just ahead of and outside the right front wheel, looking at it. Distance is measured "
                   "ahead of the wheel. Rides rigidly on the target.",
        .defaults = wheel,
        .params = Distance | Height | Fov | Drift | Lateral,
      };

      ShotTemplateParams orbit = Defaults(6.0f, 6.5f, 6.5f, 1.6f, 45.0f);
      orbit.orbitStartDegrees = 30.0f;
      orbit.orbitEndDegrees = 150.0f;
      infos[size_t(ShotTemplate::Orbit)] = {
        .name = "Orbit",
        .tooltip = "Circles the target at a constant radius and height, from the start angle to the end angle "
                   "(0 = in front, 90 = its left side), looking at it. Rides on the target with its heading smoothed.",
        .defaults = orbit,
        .params = Distance | Height | Fov | OrbitAngles,
      };

      ShotTemplateParams fixed = Defaults(5.0f, 0.0f, 0.0f, 0.0f, 35.0f);
      infos[size_t(ShotTemplate::StaticFollow)] = {
        .name = "Static Follow",
        .tooltip = "Stands where the editor camera is and pans to keep the target centred.",
        .defaults = fixed,
        .params = Fov,
      };

      ShotTemplateParams crane = Defaults(5.0f, 5.0f, 8.0f, 1.2f, 50.0f);
      crane.endHeight = 6.0f;
      crane.bearingDegrees = 35.0f;
      infos[size_t(ShotTemplate::CraneUp)] = {
        .name = "Crane Up",
        .tooltip = "Rises from low beside the target to high above it while pulling back, looking at it. "
                   "Bearing: 0 = in front, 90 = its left side. Rides on the target with its heading smoothed.",
        .defaults = crane,
        .params = Distance | EndDistance | Height | EndHeight | Bearing | Fov,
      };

      infos[size_t(ShotTemplate::FlyTo)] = {
        .name = "Fly To",
        .tooltip = "Flies from the editor camera to the destination view in world space, starting and "
                   "stopping softly. Track settings stay as they are.",
        .defaults = Defaults(3.0f, 0.0f, 0.0f, 0.0f, 50.0f),
        .params = 0,
        .needsTarget = false,
      };

      return infos;
    }
  }

  const ShotTemplateInfo& GetInfo(ShotTemplate shot)
  {
    static const std::array<ShotTemplateInfo, size_t(ShotTemplate::Count)> INFOS = BuildInfos();
    return INFOS[std::min(size_t(shot), INFOS.size() - 1)];
  }

  bool FindByName(std::string_view name, ShotTemplate& out)
  {
    auto normalize = [](std::string_view text) {
      std::string result;
      for (char c : text)
      {
        if (c == ' ' || c == '-')
          continue;
        result.push_back(c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c);
      }
      return result;
    };

    const std::string wanted = normalize(name);
    for (size_t i = 0; i < size_t(ShotTemplate::Count); i++)
    {
      if (normalize(GetInfo(ShotTemplate(i)).name) == wanted)
      {
        out = ShotTemplate(i);
        return true;
      }
    }
    return false;
  }

  GeneratedShot Generate(ShotTemplate shot, const ShotTemplateParams& p, const ShotViewPoses& views)
  {
    GeneratedShot result;

    if (!std::isfinite(p.start) || p.start < 0.0f)
    {
      result.error = "A shot cannot start before 0 s";
      return result;
    }
    if (!std::isfinite(p.duration) || p.duration < MIN_DURATION)
    {
      result.error = "A shot needs a duration of at least 0.1 s";
      return result;
    }
    if ((GetInfo(shot).params & ShotParam::Fov) != 0
      && !(p.fovDegrees >= MIN_FOV_DEGREES && p.fovDegrees <= MAX_FOV_DEGREES))
    {
      result.error = "The field of view must be between 1 and 170 degrees";
      return result;
    }

    const float t0 = p.start;
    const float t1 = p.start + p.duration;
    const float fov = glm::radians(p.fovDegrees);
    const ShotSubject& subject = p.subject;
    const glm::vec3& centre = subject.bodyCentre;
    std::vector<CameraTrackKey>& keys = result.keys;
    ShotTrackSettings& track = result.track;

    switch (shot)
    {
      case ShotTemplate::Chase:
      {
        glm::vec3 aim = centre + glm::vec3(0.0f, 0.0f, p.lookAhead);
        keys.push_back(TargetKey(t0, AroundBody(subject, 180.0f, p.distance, p.height), aim, fov));
        keys.push_back(TargetKey(t1, AroundBody(subject, 180.0f, p.endDistance, p.height), aim, fov));
        track.follow = ShotFollow::Smoothed;
        break;
      }

      case ShotTemplate::Lead:
        keys.push_back(TargetKey(t0, AroundBody(subject, 0.0f, p.distance, p.height), centre, fov));
        keys.push_back(TargetKey(t1, AroundBody(subject, 0.0f, p.endDistance, p.height), centre, fov));
        track.follow = ShotFollow::Smoothed;
        break;

      case ShotTemplate::SideTracking:
      {
        glm::vec3 position = AroundBody(subject, 90.0f * SideSign(p.side), p.distance, p.height);
        glm::vec3 slide(0.0f, 0.0f, 0.5f * p.drift);
        keys.push_back(TargetKey(t0, position - slide, centre, fov));
        keys.push_back(TargetKey(t1, position + slide, centre, fov));
        track.follow = ShotFollow::Smoothed;
        break;
      }

      case ShotTemplate::WheelCloseUpLeft:
      case ShotTemplate::WheelCloseUpRight:
      {
        const float sign = shot == ShotTemplate::WheelCloseUpLeft ? 1.0f : -1.0f;
        glm::vec3 wheel = subject.frontWheel;
        wheel.x *= sign;
        glm::vec3 position(wheel.x + sign * p.lateral, p.height, wheel.z + p.distance);
        keys.push_back(TargetKey(t0, position, wheel, fov));
        keys.push_back(TargetKey(t1, position + glm::vec3(0.0f, 0.0f, p.drift), wheel, fov));
        track.follow = ShotFollow::Full;
        break;
      }

      case ShotTemplate::Orbit:
        result.error = Orbit(p, fov, keys);
        if (result.error != nullptr)
          return result;
        track.follow = ShotFollow::Smoothed;
        break;

      case ShotTemplate::StaticFollow:
        keys.push_back(MakeKey(t0, CameraKeySpace::World, views.editorCamera.position, views.editorCamera.rotation, fov));
        keys.push_back(MakeKey(t1, CameraKeySpace::World, views.editorCamera.position, views.editorCamera.rotation, fov));
        track.aimAtTarget = true;
        track.aimOffset = centre;
        break;

      case ShotTemplate::CraneUp:
        keys.push_back(TargetKey(t0, AroundBody(subject, p.bearingDegrees, p.distance, p.height), centre, fov));
        keys.push_back(TargetKey(t1, AroundBody(subject, p.bearingDegrees, p.endDistance, p.endHeight), centre, fov));
        track.follow = ShotFollow::Smoothed;
        break;

      case ShotTemplate::FlyTo:
      {
        const CameraTrackPose& from = views.editorCamera;
        const CameraTrackPose& to = views.destination;
        if (!(from.fov > 0.0f) || !(to.fov > 0.0f))
        {
          result.error = "Fly To needs a field of view on both the editor camera and the destination";
          return result;
        }
        keys.push_back(MakeKey(t0, CameraKeySpace::World, from.position, from.rotation, from.fov));
        keys.push_back(MakeKey(t1, CameraKeySpace::World, to.position, to.rotation, to.fov));
        track.apply = false;
        break;
      }

      case ShotTemplate::Count:
        result.error = "Unknown shot template";
        return result;
    }

    EaseEnds(keys);
    return result;
  }
}

#endif
