#pragma once

#ifdef YA_EDITOR

#include "Utils/CameraTrack.h"

// Shot templates: a named camera move plus a few parameters, turned into ordinary camera keys and the
// track settings the move needs. Pure (STL/GLM only): the scene side lives in EditorCommands::AddShot.
namespace YAEngine
{
  enum class ShotTemplate : uint8_t
  {
    Chase,
    Lead,
    SideTracking,
    WheelCloseUpLeft,
    WheelCloseUpRight,
    Orbit,
    StaticFollow,
    CraneUp,
    FlyTo,
    Count
  };

  enum class ShotSide : uint8_t
  {
    Left,
    Right
  };

  // Where the interesting parts of the target sit in its frame (+X left, +Y up, +Z forward). A motion path
  // frame sits on the curve, which for the demo car is the rear axle; the defaults are that car's.
  struct ShotSubject
  {
    // Aim point of most templates and the centre distances are measured from
    glm::vec3 bodyCentre { 0.0f, 0.9f, 1.65f };
    // Left front wheel centre; the right one is mirrored across X
    glm::vec3 frontWheel { 1.18f, 0.55f, 3.44f };
    // Front and rear ends at bumper height; the demo car is 6.6 m long around the body centre
    glm::vec3 nose { 0.0f, 0.7f, 4.9f };
    glm::vec3 tail { 0.0f, 0.8f, -1.6f };
  };

  // Which ShotTemplateParams fields a template reads, so the UI shows only those
  namespace ShotParam
  {
    inline constexpr uint32_t Distance = 1 << 0;
    inline constexpr uint32_t EndDistance = 1 << 1;
    inline constexpr uint32_t Height = 1 << 2;
    inline constexpr uint32_t EndHeight = 1 << 3;
    inline constexpr uint32_t Side = 1 << 4;
    inline constexpr uint32_t Fov = 1 << 5;
    inline constexpr uint32_t OrbitAngles = 1 << 6;
    inline constexpr uint32_t Bearing = 1 << 7;
    inline constexpr uint32_t Drift = 1 << 8;
    inline constexpr uint32_t LookAhead = 1 << 9;
    inline constexpr uint32_t Lateral = 1 << 10;
  }

  // Distances and heights are metres in the target frame, angles are degrees around the body centre with
  // 0 in front of the target and 90 on its left side.
  struct ShotTemplateParams
  {
    // Timeline seconds; every key of the shot lies in [start, start + duration]
    float start = 0.0f;
    float duration = 4.0f;
    // Horizontal distance from the body centre at the first key; for a wheel close-up, how far ahead of the wheel
    float distance = 10.0f;
    // The same at the last key (Chase and Lead dolly, CraneUp pull-back)
    float endDistance = 9.0f;
    // Above the target frame origin
    float height = 2.3f;
    // At the last key (CraneUp)
    float endHeight = 6.0f;
    ShotSide side = ShotSide::Left;
    float fovDegrees = 50.0f;
    float orbitStartDegrees = 30.0f;
    float orbitEndDegrees = 150.0f;
    // CraneUp: the direction the crane stands in
    float bearingDegrees = 35.0f;
    // Slide along the target over the shot, positive forward (SideTracking, wheel close-ups)
    float drift = 2.0f;
    // Chase: the aim point is this far ahead of the body centre
    float lookAhead = 6.0f;
    // Wheel close-ups: how far outside the wheel the camera stands
    float lateral = 1.3f;
    ShotSubject subject;
  };

  enum class ShotFollow : uint8_t
  {
    // The keys are world space; the track's follow target is left as it is
    None,
    Full,
    Smoothed
  };

  struct ShotTrackSettings
  {
    // False: the template leaves every track-wide setting alone (FlyTo)
    bool apply = true;
    // Not None: the keys are Target space and the track follows the target
    ShotFollow follow = ShotFollow::None;
    float followSmoothing = 0.5f;
    // AimAt the target instead of the keyed rotation
    bool aimAtTarget = false;
    glm::vec3 aimOffset { 0.0f };
    glm::vec2 aimScreenOffset { 0.0f };
    float aimSmoothing = 0.0f;
  };

  // Poses the caller samples when the shot is added, both world space
  struct ShotViewPoses
  {
    // StaticFollow stands here, FlyTo starts here
    CameraTrackPose editorCamera;
    // FlyTo ends here, e.g. the view the track holds at the playhead
    CameraTrackPose destination;
  };

  struct ShotTemplateInfo
  {
    const char* name = nullptr;
    const char* tooltip = nullptr;
    ShotTemplateParams defaults;
    // ShotParam bits
    uint32_t params = 0;
    // Follows or aims at a target entity
    bool needsTarget = true;
  };

  struct GeneratedShot
  {
    // Sorted by time, 2 to 4 keys
    std::vector<CameraTrackKey> keys;
    ShotTrackSettings track;
    // Null on success
    const char* error = nullptr;
  };

  namespace ShotTemplates
  {
    inline constexpr float MIN_DURATION = 0.1f;

    const ShotTemplateInfo& GetInfo(ShotTemplate shot);
    // By display name, ignoring case, spaces and hyphens: "chase", "WheelCloseUpLeft"
    bool FindByName(std::string_view name, ShotTemplate& out);

    // Keys of the template over [params.start, params.start + params.duration]. Target-space keys are
    // expressed in the target frame; the rotations look down -Z with the frame's up and no roll.
    GeneratedShot Generate(ShotTemplate shot, const ShotTemplateParams& params, const ShotViewPoses& views);
  }
}

#endif
