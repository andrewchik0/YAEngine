#include "Editor/SequencerEditing.h"

#include <imgui.h>

#include "Editor/EditorCameraLayer.h"
#include "Editor/EditorCommands.h"
#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorIcons.h"
#include "Editor/Utils/EditorStyle.h"
#include "Editor/Utils/EditorWidgets.h"
#include "Editor/Utils/CameraKeyRetime.h"
#include "Scene/SequencePlayer.h"
#include "Utils/CameraOrientation.h"
#include "Utils/Log.h"

#include <glm/gtc/constants.hpp>

namespace YAEngine::SequencerEditing
{
  namespace
  {
    constexpr float FRAME_STEP = 1.0f / 30.0f;
    constexpr float SECOND_STEP = 1.0f;
    constexpr double STATUS_SECONDS = 3.0;
    constexpr float DEFAULT_SPEED = 10.0f;
    constexpr float OUTPUT_ASPECT = 16.0f / 9.0f;
    // Motion is measured as the pose change over this step, short enough to catch a quick turn
    constexpr float MOTION_STEP = 1.0f / 30.0f;
    // Fly To without keys to fly to ends this far ahead of the editor camera
    constexpr float FLY_TO_FALLBACK_DISTANCE = 5.0f;
    constexpr float ADD_SHOT_WINDOW_WIDTH = 380.0f;
    constexpr float MAX_SHOT_DURATION = 600.0f;
    constexpr float MIN_SHOT_FOV = 1.0f;
    constexpr float MAX_SHOT_FOV = 170.0f;
    constexpr float TOOLTIP_WRAP_EMS = 22.0f;

    // Focus routing also takes the keys from ImGui keyboard navigation, which would otherwise
    // press the focused button on Space and move between widgets on the arrows
    constexpr ImGuiInputFlags FOCUSED = ImGuiInputFlags_RouteFocused | ImGuiInputFlags_RouteFromRootWindow;
    constexpr ImGuiInputFlags FOCUSED_REPEAT = FOCUSED | ImGuiInputFlags_Repeat;

    bool HasTrack(Scene& scene, Entity e)
    {
      return e != entt::null && scene.GetRegistry().valid(e) && scene.HasComponent<CameraTrackComponent>(e);
    }

    bool HasPath(Scene& scene, Entity e)
    {
      return e != entt::null && scene.GetRegistry().valid(e) && scene.HasComponent<MotionPathComponent>(e);
    }

    // The key a new one at this time would land on top of, nearest first; -1 while the time is free
    template<typename Key>
    int FindKeyAt(const std::vector<Key>& keys, float time)
    {
      int hit = -1;
      float best = MIN_KEY_GAP;
      for (size_t i = 0; i < keys.size(); i++)
      {
        float distance = std::abs(keys[i].time - time);
        if (distance < best)
        {
          best = distance;
          hit = int(i);
        }
      }
      return hit;
    }

    int InsertKey(CameraTrackComponent& track, const CameraTrackKey& key)
    {
      auto it = std::upper_bound(track.keys.begin(), track.keys.end(), key.time,
        [](float time, const CameraTrackKey& other) { return time < other.time; });
      int index = int(it - track.keys.begin());
      track.keys.insert(it, key);
      return index;
    }

    int InsertSpeedKey(MotionPathComponent& path, const MotionSpeedKey& key)
    {
      auto it = std::upper_bound(path.speedKeys.begin(), path.speedKeys.end(), key.time,
        [](float time, const MotionSpeedKey& other) { return time < other.time; });
      int index = int(it - path.speedKeys.begin());
      path.speedKeys.insert(it, key);
      return index;
    }

    Entity EditorCameraEntity(EditorContext& context)
    {
      return context.editorCamera != nullptr ? context.editorCamera->GetCameraEntity() : Entity(entt::null);
    }

    // Keys are authored from the editor camera, never from the active one: while a track camera is
    // looked through it is being driven by the track, so capturing it would just record the pose the
    // track already produces. The fov comes from the track camera, which a scrub leaves on the
    // track's fov at the playhead; the editor camera's own fov is not what is authored.
    bool CaptureViewPose(EditorContext& context, Entity trackEntity, CameraTrackPose& pose)
    {
      Scene& scene = *context.scene;
      Entity source = EditorCameraEntity(context);
      if (source == entt::null || !scene.GetRegistry().valid(source))
        return false;

      // The editor camera is a root entity, so its LocalTransform is already world space
      const LocalTransform& transform = scene.GetTransform(source);
      pose.position = transform.position;
      pose.rotation = glm::normalize(transform.rotation);
      if (scene.HasComponent<CameraComponent>(trackEntity))
        pose.fov = scene.GetComponent<CameraComponent>(trackEntity).fov;
      return true;
    }

    // Every pose the editor writes into a camera key goes through here. index -1 inserts a new key
    // at time. An existing key keeps its space, interpolation and easing, and the pose is expressed
    // in that space. Returns the index of the written key.
    int WriteKeyFromWorldPose(EditorContext& context, Entity trackEntity, int index, const CameraTrackPose& world,
      float time)
    {
      Scene& scene = *context.scene;
      auto& track = scene.GetComponent<CameraTrackComponent>(trackEntity);
      if (index < 0)
      {
        CameraTrackKey key = SequencePlayer::KeyFromWorldPose(scene, trackEntity, world, GetNewKeySpace(context), time);
        return InsertKey(track, key);
      }

      CameraTrackKey& key = track.keys[index];
      const CameraTrackKey converted = SequencePlayer::KeyFromWorldPose(scene, trackEntity, world, key.space, key.time);
      key.space = converted.space;
      key.position = converted.position;
      key.rotation = converted.rotation;
      key.fov = converted.fov;
      return index;
    }

    // Widens the fov when the viewport is narrower than the output, so the whole 16:9 frame the
    // shot records stays on screen at the track's vertical fov
    float PilotViewFov(float trackFov, uint32_t viewportWidth, uint32_t viewportHeight)
    {
      if (viewportWidth == 0 || viewportHeight == 0)
        return trackFov;
      const float aspect = float(viewportWidth) / float(viewportHeight);
      if (aspect >= OUTPUT_ASPECT)
        return trackFov;
      return 2.0f * std::atan(std::tan(trackFov * 0.5f) * OUTPUT_ASPECT / aspect);
    }

    void ApplyPilotFov(EditorContext& context)
    {
      if (context.editorCamera != nullptr)
        context.editorCamera->SetFov(PilotViewFov(context.pilotTrackFov, context.viewportWidth, context.viewportHeight));
    }

    void PutPilotOnPose(EditorContext& context, const CameraTrackPose& pose)
    {
      context.editorCamera->SetPoseFromRotation(pose.position, pose.rotation);
      context.pilotTrackFov = pose.fov;
      context.pilotModified = false;
      ApplyPilotFov(context);
    }

    // A track without keys has no pose to go to; the view stays where it is
    void SyncPilotToPlayhead(EditorContext& context)
    {
      if (!context.IsPiloting() || context.editorCamera == nullptr)
        return;

      CameraTrackPose pose;
      if (SequencePlayer::EvaluateTrackPose(*context.scene, context.pilotTrack, context.sequencerPlayhead, pose))
        PutPilotOnPose(context, pose);
    }

    // World pose of the track camera as the player left it. The player writes LocalTransform;
    // after SequencePlayer::Update the systems have already refreshed the parent's world matrix.
    CameraTrackPose ReadCameraPose(Scene& scene, Entity entity)
    {
      CameraTrackPose pose;
      const LocalTransform& local = scene.GetTransform(entity);
      pose.position = local.position;
      pose.rotation = local.rotation;

      Entity parent = scene.HasComponent<HierarchyComponent>(entity) ? scene.GetHierarchy(entity).parent : Entity(entt::null);
      if (parent != entt::null && scene.HasComponent<WorldTransform>(parent))
      {
        const glm::mat4& parentWorld = scene.GetComponent<WorldTransform>(parent).world;
        glm::mat3 basis(parentWorld);
        for (int i = 0; i < 3; i++)
        {
          float length = glm::length(basis[i]);
          if (length > 1e-6f)
            basis[i] /= length;
        }
        pose.position = glm::vec3(parentWorld * glm::vec4(local.position, 1.0f));
        pose.rotation = glm::quat_cast(basis) * local.rotation;
      }
      pose.rotation = glm::normalize(pose.rotation);

      if (scene.HasComponent<CameraComponent>(entity))
        pose.fov = scene.GetComponent<CameraComponent>(entity).fov;
      return pose;
    }

    void RevealTimelineEnd(EditorContext& context)
    {
      context.sequencerRevealTime = GetTimelineEnd(context);
    }
  }

  CameraTrackComponent* GetBoundTrack(EditorContext& context)
  {
    if (context.scene == nullptr || !HasTrack(*context.scene, context.sequencerTrack))
      return nullptr;
    return &context.scene->GetComponent<CameraTrackComponent>(context.sequencerTrack);
  }

  MotionPathComponent* GetBoundPath(EditorContext& context)
  {
    if (context.scene == nullptr || !HasPath(*context.scene, context.sequencerPath))
      return nullptr;
    return &context.scene->GetComponent<MotionPathComponent>(context.sequencerPath);
  }

  void ResolveBinding(EditorContext& context)
  {
    if (context.scene == nullptr)
      return;
    Scene& scene = *context.scene;

    // Selecting a track or path entity binds it, but the binding then sticks: selecting the aim
    // target or a light to check something must not empty the panels mid-session.
    if (HasTrack(scene, context.selectedEntity))
      BindTrack(context, context.selectedEntity);
    else if (!HasTrack(scene, context.sequencerTrack))
      BindTrack(context, entt::null);

    if (HasPath(scene, context.selectedEntity))
      BindPath(context, context.selectedEntity);
    else if (!HasPath(scene, context.sequencerPath))
      BindPath(context, entt::null);

    // The only motion path binds itself: the car belongs on every shot being authored
    if (context.sequencerPath == entt::null)
    {
      Entity only = entt::null;
      int count = 0;
      for (Entity candidate : scene.GetView<MotionPathComponent>())
      {
        only = candidate;
        count++;
      }
      if (count == 1)
        BindPath(context, only);
    }

    CameraTrackComponent* track = GetBoundTrack(context);
    MotionPathComponent* path = GetBoundPath(context);
    if (track == nullptr || context.sequencerSelectedKey >= int(track->keys.size()))
      context.sequencerSelectedKey = -1;
    int rangeFirst = -1;
    int rangeLast = -1;
    if (!GetKeyRange(context, rangeFirst, rangeLast))
      ClearKeyRange(context);
    if (path == nullptr || context.sequencerSelectedSpeedKey >= int(path->speedKeys.size()))
      context.sequencerSelectedSpeedKey = -1;
    if (!GetSpeedKeyRange(context, rangeFirst, rangeLast))
      ClearSpeedKeyRange(context);
    if (path == nullptr || context.sequencerSelectedPoint >= int(path->points.size()))
      context.sequencerSelectedPoint = -1;

    // One timeline: whatever started the session, the playhead shows its time
    SequencePlayer* player = context.sequencePlayer;
    if (player != nullptr && player->IsAdvancing())
      context.sequencerPlayhead = float(player->GetTime());

    if (context.sequencerScrubRequest >= 0.0f)
    {
      float time = context.sequencerScrubRequest;
      context.sequencerScrubRequest = -1.0f;
      // The requester already posed the timeline, possibly along another track; only a pilot needs
      // its own track posed there
      if (context.IsPiloting())
        SetPlayhead(context, time);
      else
        context.sequencerPlayhead = time;
    }
  }

  void BindTrack(EditorContext& context, Entity track)
  {
    if (track == context.sequencerTrack)
      return;

    context.sequencerTrack = track;
    SelectKey(context, -1);
    if (context.IsPiloting() && context.pilotTrack != track)
      StopPilot(context);
  }

  void BindPath(EditorContext& context, Entity path)
  {
    if (path == context.sequencerPath)
      return;

    context.sequencerPath = path;
    SelectSpeedKey(context, -1);
    context.sequencerSelectedPoint = -1;
  }

  void ResetForScene(EditorContext& context)
  {
    // The editor camera entity the pilot flew is gone with the old scene; nothing to restore
    context.pilotTrack = entt::null;
    context.pilotModified = false;
    context.sequencerTrack = entt::null;
    context.sequencerPath = entt::null;
    SelectKey(context, -1);
    SelectSpeedKey(context, -1);
    context.sequencerSelectedPoint = -1;
    context.sequencerPlayhead = 0.0f;
    context.sequencerScrubRequest = -1.0f;
    context.sequencerDragActive = false;
    context.sequencerRevealTime = -1.0f;
    context.sequencerStatus.clear();
    CloseAddShotWindow();
  }

  float GetTimelineEnd(EditorContext& context)
  {
    return context.scene != nullptr ? float(SequencePlayer::TimelineDuration(*context.scene)) : 0.0f;
  }

  Entity GetPlayableTrack(EditorContext& context)
  {
    const CameraTrackComponent* track = GetBoundTrack(context);
    if (track == nullptr || track->keys.empty() || !context.scene->HasComponent<CameraComponent>(context.sequencerTrack))
      return entt::null;
    return context.sequencerTrack;
  }

  void SetPlayhead(EditorContext& context, float time)
  {
    if (context.scene == nullptr)
      return;

    context.sequencerPlayhead = glm::clamp(time, 0.0f, GetTimelineEnd(context));

    // Scrubbing poses the whole timeline. Without a session it opens a preview that leaves the
    // viewport where it is, so the frustum and path gizmos show the result from the editor camera.
    if (context.sequencePlayer != nullptr)
      context.sequencePlayer->Scrub(*context.scene, GetPlayableTrack(context), double(context.sequencerPlayhead));

    SyncPilotToPlayhead(context);
  }

  void Repose(EditorContext& context)
  {
    if (context.scene == nullptr)
      return;

    const bool keepPilot = context.pilotModified;
    if (context.sequencePlayer != nullptr)
      context.sequencePlayer->Scrub(*context.scene, GetPlayableTrack(context), double(context.sequencerPlayhead));
    if (!keepPilot)
      SyncPilotToPlayhead(context);
  }

  void TogglePlayback(EditorContext& context)
  {
    SequencePlayer* player = context.sequencePlayer;
    if (player != nullptr && player->IsAdvancing())
    {
      player->Pause();
      context.sequencerPlayhead = float(player->GetTime());
      return;
    }

    if (GetPlayUnavailableReason(context) != nullptr)
      return;

    if (!EditorCommands::PlaySequence(*player, *context.scene, GetPlayableTrack(context)))
      context.sequencerPlayhead = float(player->GetTime());
  }

  const char* GetPlayUnavailableReason(EditorContext& context)
  {
    const MotionPathComponent* path = GetBoundPath(context);
    const bool drivable = path != nullptr && path->points.size() >= 2;
    return context.sequencePlayer == nullptr || context.scene == nullptr ? "The editor has no sequence player"
      : GetPlayableTrack(context) != entt::null || drivable ? nullptr
      : GetBoundTrack(context) != nullptr ? "The camera track has no keys"
      : "The motion path needs at least two points";
  }

  void SetKey(EditorContext& context)
  {
    CameraTrackComponent* track = GetBoundTrack(context);
    if (track == nullptr)
      return;

    const Entity trackEntity = context.sequencerTrack;
    const bool piloting = context.pilotTrack == trackEntity;
    if (piloting && context.sequencePlayer != nullptr && context.sequencePlayer->IsAdvancing())
    {
      ShowStatus(context, "Pause playback to set a key: the camera is following the track");
      return;
    }

    float time = context.sequencerPlayhead;
    int existing = FindKeyAt(track->keys, time);
    if (existing >= 0 && !piloting)
    {
      SelectKey(context, existing);
      SetPlayhead(context, track->keys[existing].time);
      char text[160];
      std::snprintf(text, sizeof(text), "A key already exists at %.2f s - it is selected now; use Update From View or "
        "Pilot to replace its pose", track->keys[existing].time);
      ShowStatus(context, text);
      return;
    }

    CameraTrackPose pose;
    if (!CaptureViewPose(context, trackEntity, pose))
    {
      YA_LOG_WARN("Scene", "Sequencer: no editor camera to capture a key from");
      return;
    }

    if (existing >= 0)
    {
      // Flying has no roll, so the roll the key was authored with is laid back over the flown view
      const CameraTrackKey& key = track->keys[existing];
      time = key.time;
      const CameraTrackPose keyWorld = SequencePlayer::KeyToWorldPose(*context.scene, trackEntity, key, time);
      const float roll = YawPitchRollFromRotation(keyWorld.rotation).z;
      pose.rotation = glm::normalize(pose.rotation * glm::angleAxis(roll, glm::vec3(0.0f, 0.0f, 1.0f)));
    }

    // The pilot shows the track's fov at the playhead, whatever a stopped session left on the camera
    CameraTrackPose trackPose;
    if (piloting && SequencePlayer::EvaluateTrackPose(*context.scene, trackEntity, time, trackPose))
      pose.fov = trackPose.fov;

    SelectKey(context, WriteKeyFromWorldPose(context, trackEntity, existing, pose, time));
    RevealTimelineEnd(context);
    SetPlayhead(context, time);
  }

  void UpdateKeyFromView(EditorContext& context, int keyIndex)
  {
    CameraTrackComponent* track = GetBoundTrack(context);
    if (track == nullptr || keyIndex < 0 || keyIndex >= int(track->keys.size()))
      return;

    CameraTrackPose pose;
    if (!CaptureViewPose(context, context.sequencerTrack, pose))
      return;

    WriteKeyFromWorldPose(context, context.sequencerTrack, keyIndex, pose, track->keys[keyIndex].time);
    Repose(context);
  }

  void SetKeyWorldPose(EditorContext& context, int keyIndex, const CameraTrackPose& world)
  {
    CameraTrackComponent* track = GetBoundTrack(context);
    if (track == nullptr || keyIndex < 0 || keyIndex >= int(track->keys.size()))
      return;
    WriteKeyFromWorldPose(context, context.sequencerTrack, keyIndex, world, track->keys[keyIndex].time);
  }

  void KeyEdited(EditorContext& context, int keyIndex)
  {
    const CameraTrackComponent* track = GetBoundTrack(context);
    const bool onKey = track != nullptr && keyIndex >= 0 && keyIndex < int(track->keys.size())
      && std::abs(track->keys[keyIndex].time - context.sequencerPlayhead) < MIN_KEY_GAP * 0.5f;
    if (onKey && context.pilotTrack == context.sequencerTrack)
      context.pilotModified = false;
    Repose(context);
  }

  const char* GetTargetSpaceUnavailableReason(EditorContext& context)
  {
    const CameraTrackComponent* track = GetBoundTrack(context);
    if (track == nullptr)
      return "Bind a camera track first";
    if (track->followTargetName.empty())
      return "Pick a Follow Target in the Shot group first";

    CameraTargetFrame frame;
    if (!SequencePlayer::ResolveTargetFrame(*context.scene, *track, context.sequencerPlayhead, frame))
      return "The Follow Target names no entity the camera can follow";
    return nullptr;
  }

  CameraKeySpace GetNewKeySpace(EditorContext& context)
  {
    return GetTargetSpaceUnavailableReason(context) == nullptr ? CameraKeySpace::Target : CameraKeySpace::World;
  }

  void SetKeySpace(EditorContext& context, int keyIndex, CameraKeySpace space)
  {
    CameraTrackComponent* track = GetBoundTrack(context);
    if (track == nullptr || keyIndex < 0 || keyIndex >= int(track->keys.size()))
      return;

    Scene& scene = *context.scene;
    const Entity trackEntity = context.sequencerTrack;
    CameraTrackKey& key = track->keys[keyIndex];
    if (key.space == space)
      return;

    const CameraTrackPose world = SequencePlayer::KeyToWorldPose(scene, trackEntity, key, key.time);
    const CameraTrackKey converted = SequencePlayer::KeyFromWorldPose(scene, trackEntity, world, space, key.time);
    key.space = converted.space;
    key.position = converted.position;
    key.rotation = converted.rotation;
    KeyEdited(context, keyIndex);
  }

  void SelectKey(EditorContext& context, int index)
  {
    context.sequencerSelectedKey = index;
    ClearKeyRange(context);
  }

  void ExtendKeyRange(EditorContext& context, int index)
  {
    const CameraTrackComponent* track = GetBoundTrack(context);
    if (track == nullptr || index < 0 || index >= int(track->keys.size()))
      return;

    int first = -1;
    int last = -1;
    if (!GetKeyRange(context, first, last))
    {
      const int selected = context.sequencerSelectedKey;
      context.sequencerKeyRangeAnchor = selected >= 0 && selected < int(track->keys.size()) ? selected : index;
    }
    context.sequencerSelectedKey = index;
    context.sequencerKeyRangeKeyCount = track->keys.size();
    if (context.sequencerKeyRangeAnchor == index)
      ClearKeyRange(context);
  }

  void ClearKeyRange(EditorContext& context)
  {
    context.sequencerKeyRangeAnchor = -1;
  }

  bool GetKeyRange(EditorContext& context, int& first, int& last)
  {
    const CameraTrackComponent* track = GetBoundTrack(context);
    const int count = track != nullptr ? int(track->keys.size()) : 0;
    const int anchor = context.sequencerKeyRangeAnchor;
    const int selected = context.sequencerSelectedKey;
    if (anchor < 0 || anchor >= count || selected < 0 || selected >= count || anchor == selected
      || context.sequencerKeyRangeKeyCount != size_t(count))
    {
      return false;
    }

    first = std::min(anchor, selected);
    last = std::max(anchor, selected);
    return true;
  }

  void SelectSpeedKey(EditorContext& context, int index)
  {
    context.sequencerSelectedSpeedKey = index;
    ClearSpeedKeyRange(context);
  }

  void ExtendSpeedKeyRange(EditorContext& context, int index)
  {
    const MotionPathComponent* path = GetBoundPath(context);
    if (path == nullptr || index < 0 || index >= int(path->speedKeys.size()))
      return;

    int first = -1;
    int last = -1;
    if (!GetSpeedKeyRange(context, first, last))
    {
      const int selected = context.sequencerSelectedSpeedKey;
      context.sequencerSpeedKeyRangeAnchor = selected >= 0 && selected < int(path->speedKeys.size()) ? selected : index;
    }
    context.sequencerSelectedSpeedKey = index;
    context.sequencerSpeedKeyRangeKeyCount = path->speedKeys.size();
    if (context.sequencerSpeedKeyRangeAnchor == index)
      ClearSpeedKeyRange(context);
  }

  void ClearSpeedKeyRange(EditorContext& context)
  {
    context.sequencerSpeedKeyRangeAnchor = -1;
  }

  bool GetSpeedKeyRange(EditorContext& context, int& first, int& last)
  {
    const MotionPathComponent* path = GetBoundPath(context);
    const int count = path != nullptr ? int(path->speedKeys.size()) : 0;
    const int anchor = context.sequencerSpeedKeyRangeAnchor;
    const int selected = context.sequencerSelectedSpeedKey;
    if (anchor < 0 || anchor >= count || selected < 0 || selected >= count || anchor == selected
      || context.sequencerSpeedKeyRangeKeyCount != size_t(count))
    {
      return false;
    }

    first = std::min(anchor, selected);
    last = std::max(anchor, selected);
    return true;
  }

  namespace
  {
    // The drive Match Car Pace follows: the follow target's when it has one, else the bound path's
    Entity FindPaceCar(EditorContext& context, const SequencePlayer::TrackTargets& targets)
    {
      Scene& scene = *context.scene;
      auto drivable = [&scene](Entity e) {
        return HasPath(scene, e) && scene.GetComponent<MotionPathComponent>(e).points.size() >= 2;
      };
      if (drivable(targets.follow))
        return targets.follow;
      if (drivable(context.sequencerPath))
        return context.sequencerPath;
      return entt::null;
    }

    // How far along its path the car is at a time, as the player poses it: it stops at the end
    float CarDistance(const MotionPathComponent& path, float time)
    {
      return std::min(EvaluateMotionTiming(path.speedKeys, time).distance, path.GetTable().length);
    }

    enum class RetimeRange : uint8_t { Selection, WholeTrack, Explicit };

    // -1, -1 becomes the key range, else the whole track. False while the indices name no range.
    bool ResolveRetimeRange(EditorContext& context, const CameraTrackComponent& track, int& first, int& last,
      RetimeRange& kind)
    {
      const int count = int(track.keys.size());
      kind = RetimeRange::Explicit;
      if (first < 0 && last < 0)
      {
        kind = RetimeRange::Selection;
        if (!GetKeyRange(context, first, last))
        {
          kind = RetimeRange::WholeTrack;
          first = 0;
          last = count - 1;
        }
      }
      return first >= 0 && last < count && first < last;
    }

    std::string DescribeRetimeRange(RetimeRange kind, int first, int last)
    {
      char text[48];
      if (kind == RetimeRange::Selection)
        std::snprintf(text, sizeof(text), "the selected keys %d-%d", first + 1, last + 1);
      else if (kind == RetimeRange::WholeTrack)
        std::snprintf(text, sizeof(text), "all keys (1-%d)", last + 1);
      else
        std::snprintf(text, sizeof(text), "keys %d-%d", first + 1, last + 1);
      return text;
    }
  }

  std::string GetRetimeUnavailableReason(EditorContext& context, RetimePace pace, int first, int last)
  {
    const CameraTrackComponent* track = GetBoundTrack(context);
    if (track == nullptr)
      return "Bind a camera track first";

    RetimeRange kind = RetimeRange::Explicit;
    const bool valid = ResolveRetimeRange(context, *track, first, last, kind);
    if (!valid && kind == RetimeRange::Explicit)
      return "The key range is not on the track";
    if (!valid || last - first < 2)
    {
      return kind == RetimeRange::Selection
        ? "Select at least three keys: the first and the last selected key keep their times"
        : "Needs at least three keys: the first and the last keep their times";
    }

    const std::vector<CameraTrackKey>& keys = track->keys;
    if (keys[size_t(last)].time - keys[size_t(first)].time < MIN_KEY_GAP * float(last - first))
      return "The keys are too close together in time to be retimed";

    if (pace == RetimePace::Car)
    {
      Scene& scene = *context.scene;
      const Entity car = FindPaceCar(context, SequencePlayer::FindTrackTargets(scene, context.sequencerTrack));
      if (car == entt::null)
      {
        return "No car to keep pace with: the Follow Target does not drive along a motion path, and no motion "
               "path is bound in the Sequencer";
      }

      const MotionPathComponent& path = scene.GetComponent<MotionPathComponent>(car);
      if (CarDistance(path, keys[size_t(last)].time) - CarDistance(path, keys[size_t(first)].time)
        < CameraKeyRetime::MIN_PACE)
      {
        return "'" + EditorCommands::GetEntityDisplayName(scene.GetRegistry(), car) + "' does not move between "
          + DescribeRetimeRange(kind, first, last);
      }
    }
    return {};
  }

  RetimeResult RetimeKeys(EditorContext& context, RetimePace pace, int first, int last)
  {
    RetimeResult result;
    result.error = GetRetimeUnavailableReason(context, pace, first, last);
    if (!result.error.empty())
    {
      ShowStatus(context, result.error.c_str());
      return result;
    }

    Scene& scene = *context.scene;
    const Entity trackEntity = context.sequencerTrack;
    CameraTrackComponent& track = *GetBoundTrack(context);
    RetimeRange kind = RetimeRange::Explicit;
    ResolveRetimeRange(context, track, first, last, kind);
    result.first = first;
    result.last = last;

    const SequencePlayer::TrackTargets targets = SequencePlayer::FindTrackTargets(scene, trackEntity);
    const CameraKeyRetime::FrameAt frameAt = [&scene, trackEntity, &targets](float time, CameraTargetFrame& out) {
      return SequencePlayer::ResolveTargetFrame(scene, trackEntity, targets, time, out);
    };

    Entity car = entt::null;
    CameraKeyRetime::Pace retimePace = CameraKeyRetime::SteadyPace();
    if (pace == RetimePace::Car)
    {
      car = FindPaceCar(context, targets);
      const MotionPathComponent& path = scene.GetComponent<MotionPathComponent>(car);
      retimePace.progress = [&path](float time) { return CarDistance(path, time); };
      retimePace.timeAt = [&path](float value, float after) {
        if (value <= CarDistance(path, after))
          return after;
        // The drive stops at the end of the path, whatever the speed keys say after that
        if (value > path.GetTable().length)
          return std::numeric_limits<float>::infinity();
        return std::max(MotionTimeAtDistance(path.speedKeys, value), after);
      };
    }

    const CameraKeyRetime::Result retimed = CameraKeyRetime::Retime(track.keys, size_t(first), size_t(last),
      MIN_KEY_GAP, frameAt, retimePace);
    const std::string range = DescribeRetimeRange(kind, first, last);
    const std::string carName = car != entt::null
      ? EditorCommands::GetEntityDisplayName(scene.GetRegistry(), car) : std::string();

    switch (retimed.error)
    {
      case CameraKeyRetime::Error::None:
        break;
      case CameraKeyRetime::Error::NoTravel:
        result.error = "The camera does not move between " + range + ", so there is nothing to retime";
        break;
      case CameraKeyRetime::Error::NoPace:
        result.error = pace == RetimePace::Car ? "'" + carName + "' does not move between " + range
          : "Every segment between " + range + " holds still";
        break;
      case CameraKeyRetime::Error::TooShort:
        result.error = "The keys are too close together in time to be retimed";
        break;
      case CameraKeyRetime::Error::TooFewKeys:
        result.error = "Needs at least three keys: the first and the last keep their times";
        break;
    }
    if (!result.error.empty())
    {
      ShowStatus(context, result.error.c_str());
      return result;
    }

    result.keyTimesBefore.reserve(retimed.times.size());
    for (int i = first; i <= last; i++)
      result.keyTimesBefore.push_back(track.keys[size_t(i)].time);
    for (int i = first + 1; i < last; i++)
      track.keys[size_t(i)].time = retimed.times[size_t(i - first)];
    result.keyTimesAfter = retimed.times;
    result.mode = retimed.worldSegments == 0 ? "target" : retimed.relativeSegments == 0 ? "world" : "mixed";
    result.iterations = retimed.passes;
    result.settled = retimed.settled;
    result.shift = retimed.shift;

    char text[256];
    if (pace == RetimePace::Car)
    {
      const MotionPathComponent& path = scene.GetComponent<MotionPathComponent>(car);
      const float covered = CarDistance(path, track.keys[size_t(last)].time)
        - CarDistance(path, track.keys[size_t(first)].time);
      std::snprintf(text, sizeof(text), "Retimed %s: the camera now speeds up and slows down with '%s', which "
        "covers %.1f m in that time", range.c_str(), carName.c_str(), covered);
    }
    else if (retimed.worldSegments == 0)
    {
      std::snprintf(text, sizeof(text), "Retimed %s: the camera now moves around '%s' at a steady %.1f m/s",
        range.c_str(), track.followTargetName.c_str(), retimed.travel / retimed.pace);
    }
    else if (retimed.relativeSegments == 0)
    {
      std::snprintf(text, sizeof(text), "Retimed %s: the camera now moves through the world at a steady %.1f m/s",
        range.c_str(), retimed.travel / retimed.pace);
    }
    else
    {
      std::snprintf(text, sizeof(text), "Retimed %s to a steady pace: relative keys are measured around '%s', "
        "the rest in the world", range.c_str(), track.followTargetName.c_str());
    }

    std::string status = text;
    if (!retimed.settled)
    {
      char unsettled[160];
      std::snprintf(unsettled, sizeof(unsettled), ". The timing did not settle: pressing it again may still move "
        "keys by up to %.2f s", retimed.shift);
      status += unsettled;
      YA_LOG_WARN("Editor", "Sequencer: retime of keys %d-%d did not settle after %d passes (last move %.3f s)",
        first, last, int(retimed.passes), retimed.shift);
    }
    ShowStatus(context, status.c_str());
    YA_LOG_INFO("Editor", "Sequencer: %s retimed keys %d-%d of '%s' (%s, %d passes)",
      pace == RetimePace::Car ? "Match Car Pace" : "Even Out Speed", first, last,
      EditorCommands::GetEntityDisplayName(scene.GetRegistry(), trackEntity).c_str(), result.mode,
      int(result.iterations));
    Repose(context);
    return result;
  }

  void AddSpeedKey(EditorContext& context)
  {
    MotionPathComponent* path = GetBoundPath(context);
    if (path == nullptr)
      return;

    const float playhead = context.sequencerPlayhead;
    int existing = FindKeyAt(path->speedKeys, playhead);
    if (existing >= 0)
    {
      SelectSpeedKey(context, existing);
      SetPlayhead(context, path->speedKeys[existing].time);
      char text[128];
      std::snprintf(text, sizeof(text), "A speed key already exists at %.2f s - it is selected now; edit its Speed "
        "instead", path->speedKeys[existing].time);
      ShowStatus(context, text);
      return;
    }

    MotionSpeedKey key;
    key.time = playhead;
    key.speed = path->speedKeys.empty() ? DEFAULT_SPEED : EvaluateMotionTiming(path->speedKeys, playhead).speed;
    SelectSpeedKey(context, InsertSpeedKey(*path, key));

    RevealTimelineEnd(context);
    SetPlayhead(context, key.time);
  }

  void StepToNeighbourKey(EditorContext& context, int direction)
  {
    const CameraTrackComponent* track = GetBoundTrack(context);
    if (track == nullptr)
      return;

    const int count = int(track->keys.size());
    const int selected = context.sequencerSelectedKey;
    const float playhead = context.sequencerPlayhead;
    int target = -1;
    const bool onSelected = selected >= 0 && selected < count
      && std::abs(track->keys[selected].time - playhead) < MIN_KEY_GAP * 0.5f;
    if (onSelected)
    {
      target = selected + direction;
    }
    else if (direction > 0)
    {
      for (int i = 0; i < count && target < 0; i++)
      {
        if (track->keys[i].time > playhead)
          target = i;
      }
    }
    else
    {
      for (int i = count - 1; i >= 0 && target < 0; i--)
      {
        if (track->keys[i].time < playhead)
          target = i;
      }
    }

    if (target < 0 || target >= count)
      return;

    SelectKey(context, target);
    SetPlayhead(context, track->keys[target].time);
  }

  void ShowStatus(EditorContext& context, const char* text)
  {
    context.sequencerStatus = text;
    context.sequencerStatusExpiry = ImGui::GetTime() + STATUS_SECONDS;
  }

  void HandleHotkeys(EditorContext& context)
  {
    // K or [ ] mid-drag would change the keys or the selection under the dragged index
    if (context.scene == nullptr || ImGui::GetIO().WantTextInput || context.sequencerDragActive)
      return;

    if (ImGui::Shortcut(ImGuiKey_Space, FOCUSED))
      TogglePlayback(context);

    // Claimed only while there is a key range or a pilot to end, so Esc keeps its navigation meaning
    // otherwise; the range goes first. Over the viewport the game's own Esc camera swap fires too and
    // cannot be held back, so there it is left to the game.
    int rangeFirst = -1;
    int rangeLast = -1;
    const bool anyRange = GetKeyRange(context, rangeFirst, rangeLast) || GetSpeedKeyRange(context, rangeFirst, rangeLast);
    if (!context.viewportHovered && anyRange && ImGui::Shortcut(ImGuiKey_Escape, FOCUSED))
    {
      ClearKeyRange(context);
      ClearSpeedKeyRange(context);
    }
    else if (context.IsPiloting() && !context.viewportHovered && ImGui::Shortcut(ImGuiKey_Escape, FOCUSED))
    {
      StopPilot(context);
    }

    if (GetBoundTrack(context) != nullptr)
    {
      if (ImGui::Shortcut(ImGuiKey_K, FOCUSED))
        SetKey(context);

      if (ImGui::Shortcut(ImGuiKey_P, FOCUSED))
      {
        if (context.pilotTrack == context.sequencerTrack)
          StopPilot(context);
        else if (const char* reason = GetPilotUnavailableReason(context, context.sequencerTrack))
          ShowStatus(context, reason);
        else
          StartPilot(context, context.sequencerTrack);
      }

      if (ImGui::Shortcut(ImGuiKey_LeftBracket, FOCUSED_REPEAT))
        StepToNeighbourKey(context, -1);
      if (ImGui::Shortcut(ImGuiKey_RightBracket, FOCUSED_REPEAT))
        StepToNeighbourKey(context, 1);
    }

    // Over the viewport the arrows drive the game
    if (context.viewportHovered)
      return;

    float step = 0.0f;
    if (ImGui::Shortcut(ImGuiKey_LeftArrow, FOCUSED_REPEAT))
      step -= FRAME_STEP;
    if (ImGui::Shortcut(ImGuiKey_RightArrow, FOCUSED_REPEAT))
      step += FRAME_STEP;
    if (ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_LeftArrow, FOCUSED_REPEAT))
      step -= SECOND_STEP;
    if (ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_RightArrow, FOCUSED_REPEAT))
      step += SECOND_STEP;
    if (step != 0.0f)
      SetPlayhead(context, context.sequencerPlayhead + step);
  }

  void HandleViewportHotkeys(EditorContext& context)
  {
    // A held button is a gizmo drag or a flight in progress
    if (!context.IsPiloting() || ImGui::GetIO().WantTextInput || ImGui::IsAnyMouseDown())
      return;

    if (ImGui::Shortcut(ImGuiKey_P, FOCUSED))
      StopPilot(context);
    else if (ImGui::Shortcut(ImGuiKey_K, FOCUSED) && context.pilotTrack == context.sequencerTrack)
      SetKey(context);
  }

  const char* GetPilotUnavailableReason(EditorContext& context, Entity track)
  {
    if (context.scene == nullptr || context.editorCamera == nullptr || EditorCameraEntity(context) == entt::null)
      return "The editor camera is not available";
    if (!HasTrack(*context.scene, track))
      return "Bind a camera track first";
    if (!context.scene->HasComponent<CameraComponent>(track))
      return "The track entity has no camera";
    return nullptr;
  }

  bool StartPilot(EditorContext& context, Entity track)
  {
    if (GetPilotUnavailableReason(context, track) != nullptr)
      return false;
    if (context.pilotTrack == track)
      return true;

    Scene& scene = *context.scene;

    // Switching tracks restores first, so the pose put back at the end is the one before any pilot
    StopPilot(context);
    BindTrack(context, track);
    // A selected entity with another track would take the binding back on the next frame
    if (context.selectedEntity != track && HasTrack(scene, context.selectedEntity))
      context.SelectEntity(track);
    context.StopCameraPreview();

    EditorCameraLayer& camera = *context.editorCamera;
    context.pilotRestorePosition = camera.GetPosition();
    context.pilotRestoreYaw = camera.GetYaw();
    context.pilotRestorePitch = camera.GetPitch();
    context.pilotRestoreFov = camera.GetFov();
    context.pilotTrack = track;
    context.pilotModified = false;
    // Replaced by the track's fov at the playhead once the track has keys
    context.pilotTrackFov = scene.GetComponent<CameraComponent>(track).fov;

    Entity editorEntity = camera.GetCameraEntity();
    if (scene.GetActiveCamera() != editorEntity)
      scene.SetActiveCamera(editorEntity);

    ApplyPilotFov(context);
    SetPlayhead(context, context.sequencerPlayhead);
    return true;
  }

  void StopPilot(EditorContext& context)
  {
    if (!context.IsPiloting())
      return;

    Entity track = context.pilotTrack;
    context.pilotTrack = entt::null;
    context.pilotModified = false;

    EditorCameraLayer* camera = context.editorCamera;
    if (camera == nullptr || context.scene == nullptr)
      return;

    camera->SetInputLocked(false);
    camera->SetPose(context.pilotRestorePosition, context.pilotRestoreYaw, context.pilotRestorePitch);
    camera->SetFov(context.pilotRestoreFov);

    // A session that took the track camera for playback expects to show it; the pilot only kept
    // the editor camera in front of it
    Scene& scene = *context.scene;
    SequencePlayer* player = context.sequencePlayer;
    if (player != nullptr && player->HoldsCamera() && player->GetCameraTrack() == track
      && scene.GetRegistry().valid(track) && !context.IsPreviewingCamera())
    {
      scene.SetActiveCamera(track);
    }
  }

  void UpdatePilot(EditorContext& context)
  {
    if (!context.IsPiloting())
      return;

    Scene& scene = *context.scene;
    Entity track = context.pilotTrack;
    Entity editorEntity = EditorCameraEntity(context);
    if (!HasTrack(scene, track) || !scene.HasComponent<CameraComponent>(track) || context.sequencerTrack != track
      || context.IsPreviewingCamera() || editorEntity == entt::null)
    {
      StopPilot(context);
      return;
    }

    SequencePlayer* player = context.sequencePlayer;
    const bool held = player != nullptr && player->HoldsCamera();
    if (held && player->GetCameraTrack() != track)
    {
      // Another shot took the viewport (F9 in the game layer)
      StopPilot(context);
      return;
    }
    if (held)
    {
      // Playback hands the viewport to the track camera; the pilot keeps looking through the editor camera
      if (scene.GetActiveCamera() != editorEntity)
        scene.SetActiveCamera(editorEntity);
    }
    else if (scene.GetActiveCamera() != editorEntity)
    {
      StopPilot(context);
      return;
    }

    const bool following = player != nullptr && player->IsAdvancing() && player->GetCameraTrack() == track;
    context.editorCamera->SetInputLocked(following);
    if (following)
      PutPilotOnPose(context, ReadCameraPose(scene, track));
    else
      ApplyPilotFov(context);
  }

  void NotifyEditorCameraFlown(EditorContext& context)
  {
    if (context.IsPiloting())
      context.pilotModified = true;
  }

  void GetPilotOutputFrame(float viewportWidth, float viewportHeight, float& outX, float& outY,
    float& outWidth, float& outHeight)
  {
    outWidth = viewportWidth;
    outHeight = viewportHeight;
    if (viewportHeight > 0.0f && viewportWidth / viewportHeight > OUTPUT_ASPECT)
      outWidth = viewportHeight * OUTPUT_ASPECT;
    else
      outHeight = viewportWidth / OUTPUT_ASPECT;
    outX = (viewportWidth - outWidth) * 0.5f;
    outY = (viewportHeight - outHeight) * 0.5f;
  }

  namespace
  {
    // FNV-1a, field by field so struct padding never reaches it
    struct MotionHasher
    {
      uint64_t value = 1469598103934665603ull;

      void Bytes(const void* data, size_t size)
      {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; i++)
        {
          value ^= bytes[i];
          value *= 1099511628211ull;
        }
      }

      template<typename T> requires std::is_arithmetic_v<T> || std::is_enum_v<T>
      void Add(T v) { Bytes(&v, sizeof(v)); }

      void Add(const glm::vec2& v) { Add(v.x); Add(v.y); }
      void Add(const glm::vec3& v) { Add(v.x); Add(v.y); Add(v.z); }
      void Add(const glm::quat& q) { Add(q.x); Add(q.y); Add(q.z); Add(q.w); }
      void Add(const std::string& text) { Add(text.size()); Bytes(text.data(), text.size()); }
    };

    void HashTargetEntity(MotionHasher& hasher, Scene& scene, Entity entity)
    {
      hasher.Add(entt::to_integral(entity));
      if (entity == entt::null || !scene.GetRegistry().valid(entity))
        return;

      if (scene.HasComponent<MotionPathComponent>(entity))
      {
        const auto& path = scene.GetComponent<MotionPathComponent>(entity);
        hasher.Add(path.points.size());
        for (const glm::vec3& point : path.points)
          hasher.Add(point);
        hasher.Add(path.speedKeys.size());
        for (const MotionSpeedKey& key : path.speedKeys)
        {
          hasher.Add(key.time);
          hasher.Add(key.speed);
        }
        hasher.Add(path.curvatureSmoothing);
        if (path.points.size() >= 2)
          return;
      }

      if (scene.HasComponent<WorldTransform>(entity))
      {
        const glm::mat4& world = scene.GetComponent<WorldTransform>(entity).world;
        hasher.Bytes(&world[0][0], sizeof(float) * 16);
      }
    }

    // The editor camera pose; the fov is what the viewport shows, the pilot's track fov while piloting
    bool EditorCameraPose(EditorContext& context, CameraTrackPose& pose)
    {
      Scene& scene = *context.scene;
      Entity source = EditorCameraEntity(context);
      if (source == entt::null || !scene.GetRegistry().valid(source))
        return false;

      const LocalTransform& transform = scene.GetTransform(source);
      pose.position = transform.position;
      pose.rotation = glm::normalize(transform.rotation);
      pose.fov = context.IsPiloting() ? context.pilotTrackFov : context.editorCamera->GetFov();
      return true;
    }

    void ItemTooltip(const char* text)
    {
      if (text == nullptr || !ImGui::BeginItemTooltip())
        return;
      ImGui::PushTextWrapPos(ImGui::GetFontSize() * TOOLTIP_WRAP_EMS);
      ImGui::TextUnformatted(text);
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }

    struct AddShotWindowState
    {
      bool open = false;
      bool appearing = false;
      ShotTemplate shot = ShotTemplate::Chase;
      ShotTemplateParams params;
      Entity target { entt::null };
      Entity camera { entt::null };
      bool newCamera = true;
      std::string error;
    };

    AddShotWindowState s_AddShot;

    const EditorWidgets::EnumOption SHOT_SIDES[] = {
      { .label = "Left", .tooltip = "On the target's left side" },
      { .label = "Right", .tooltip = "On the target's right side" },
    };

    void OpenAddShotWindow(EditorContext& context, ShotTemplate shot)
    {
      AddShotWindowState& state = s_AddShot;
      const bool keep = state.open;
      const float start = keep ? state.params.start : context.sequencerPlayhead;

      state.shot = shot;
      state.params = ShotTemplates::GetInfo(shot).defaults;
      state.params.start = start;
      state.error.clear();
      state.appearing = true;
      state.open = true;
      if (keep)
        return;

      Scene& scene = *context.scene;
      const bool pathBound = GetBoundPath(context) != nullptr;
      state.target = pathBound ? context.sequencerPath : Entity(entt::null);
      const bool trackBound = GetBoundTrack(context) != nullptr
        && scene.HasComponent<CameraComponent>(context.sequencerTrack);
      state.camera = trackBound ? context.sequencerTrack : Entity(entt::null);
      state.newCamera = !trackBound;
    }

    void DrawAddShotMoveRows(ShotTemplateParams& params, uint32_t mask)
    {
      using namespace EditorWidgets;

      auto has = [mask](uint32_t bit) { return (mask & bit) != 0; };
      if (has(ShotParam::Distance))
      {
        PropertyFloat("Distance", params.distance, {
          .min = 0.0f, .max = 500.0f, .speed = 0.05f, .format = "%.2f", .unit = "m",
          .tooltip = "How far from the middle of the target the camera starts, measured along the ground. For a wheel "
                     "close-up: how far ahead of the wheel." });
      }
      if (has(ShotParam::EndDistance))
      {
        PropertyFloat("End Distance", params.endDistance, {
          .min = 0.0f, .max = 500.0f, .speed = 0.05f, .format = "%.2f", .unit = "m",
          .tooltip = "The same distance at the end of the shot; different from Distance, the camera moves closer or "
                     "further during the shot." });
      }
      if (has(ShotParam::Height))
      {
        PropertyFloat("Height", params.height, {
          .min = -50.0f, .max = 500.0f, .speed = 0.02f, .format = "%.2f", .unit = "m",
          .tooltip = "Camera height above the ground under the target (the target's own origin)." });
      }
      if (has(ShotParam::EndHeight))
      {
        PropertyFloat("End Height", params.endHeight, {
          .min = -50.0f, .max = 500.0f, .speed = 0.02f, .format = "%.2f", .unit = "m",
          .tooltip = "Camera height at the end of the shot." });
      }
      if (has(ShotParam::Side))
      {
        PropertyEnum("Side", params.side, SHOT_SIDES, { .tooltip = "Which side of the target the camera rides on." });
      }
      if (has(ShotParam::OrbitAngles))
      {
        PropertyFloat("Orbit Start", params.orbitStartDegrees, {
          .min = -720.0f, .max = 720.0f, .speed = 0.5f, .format = "%.0f", .unit = "deg",
          .tooltip = "Where around the target the camera starts: 0 = in front of it, 90 = its left side, 180 = behind "
                     "it, -90 = its right side." });
        PropertyFloat("Orbit End", params.orbitEndDegrees, {
          .min = -720.0f, .max = 720.0f, .speed = 0.5f, .format = "%.0f", .unit = "deg",
          .tooltip = "Where around the target the camera ends, in the same angles. The camera circles from Orbit Start "
                     "to Orbit End." });
      }
      if (has(ShotParam::Bearing))
      {
        PropertyFloat("Bearing", params.bearingDegrees, {
          .min = -180.0f, .max = 180.0f, .speed = 0.5f, .format = "%.0f", .unit = "deg",
          .tooltip = "Which direction from the target the camera rises in: 0 = in front of it, 90 = its left side, "
                     "180 = behind it." });
      }
      if (has(ShotParam::Drift))
      {
        PropertyFloat("Drift", params.drift, {
          .min = -50.0f, .max = 50.0f, .speed = 0.02f, .format = "%.2f", .unit = "m",
          .tooltip = "How far the camera slides along the target during the shot; positive moves toward its front." });
      }
      if (has(ShotParam::LookAhead))
      {
        PropertyFloat("Look Ahead", params.lookAhead, {
          .min = -50.0f, .max = 100.0f, .speed = 0.05f, .format = "%.2f", .unit = "m",
          .tooltip = "The camera looks at a point this far in front of the target, so the road ahead is in the frame." });
      }
      if (has(ShotParam::Lateral))
      {
        PropertyFloat("Lateral", params.lateral, {
          .min = 0.0f, .max = 20.0f, .speed = 0.02f, .format = "%.2f", .unit = "m",
          .tooltip = "How far outside the wheel the camera stands." });
      }
      if (has(ShotParam::Fov))
      {
        PropertyFloat("FOV", params.fovDegrees, {
          .min = MIN_SHOT_FOV, .max = MAX_SHOT_FOV, .speed = 0.25f, .format = "%.1f", .unit = "deg",
          .tooltip = "Vertical field of view of the shot. Smaller zooms in." });
      }
    }

    void DrawAddShotContents(EditorContext& context)
    {
      using namespace EditorWidgets;

      AddShotWindowState& state = s_AddShot;
      Scene& scene = *context.scene;
      entt::registry& registry = scene.GetRegistry();

      static const std::array<EnumOption, size_t(ShotTemplate::Count)> TEMPLATE_OPTIONS = [] {
        std::array<EnumOption, size_t(ShotTemplate::Count)> options;
        for (size_t i = 0; i < options.size(); i++)
        {
          const ShotTemplateInfo& info = ShotTemplates::GetInfo(ShotTemplate(i));
          options[i] = { .label = info.name, .tooltip = info.tooltip };
        }
        return options;
      }();

      BeginPropertyScope();
      int32_t shotIndex = int32_t(state.shot);
      if (PropertyEnum("Template", shotIndex, TEMPLATE_OPTIONS, {
        .tooltip = "The camera move to add. Picking another one resets the move settings below to its defaults." }).changed)
      {
        OpenAddShotWindow(context, ShotTemplate(shotIndex));
      }

      const ShotTemplateInfo& info = ShotTemplates::GetInfo(state.shot);
      PropertyStatus(nullptr, info.tooltip);

      if (PropertyEntity("Target", state.target, scene, {
        .allowNone = true,
        .uniqueNames = true,
        .filter = [&state](Entity candidate) { return state.newCamera || candidate != state.camera; },
        .tooltip = "What the camera follows or turns toward, usually the car. The shot refers to it by name, so no "
                   "other entity may share that name. Defaults to the motion path bound in the Sequencer.",
        .disabledReason = info.needsTarget ? nullptr : "This move does not follow anything" }).changed)
      {
        state.error.clear();
      }

      if (PropertyBool("New Camera", state.newCamera, {
        .tooltip = "Creates a camera named Shot N for this shot. Turn it off to add the shot to an existing camera "
                   "instead; that camera's keys inside the shot's time range are replaced." }).changed)
      {
        state.error.clear();
      }

      if (PropertyEntity("Camera", state.camera, scene, {
        .allowNone = false,
        .filter = [&registry](Entity candidate) {
          return registry.all_of<CameraComponent>(candidate) && !registry.all_of<MotionPathComponent>(candidate);
        },
        .tooltip = "The camera that gets the shot. Defaults to the camera bound in the Sequencer.",
        .disabledReason = state.newCamera ? "Turn off New Camera to add the shot to an existing camera" : nullptr }).changed)
      {
        state.error.clear();
      }

      PropertyFloat("Start", state.params.start, {
        .min = 0.0f, .max = MAX_KEY_ADVANCE, .speed = 0.02f, .format = "%.2f", .unit = "s",
        .tooltip = "Timeline second the shot begins at. Defaults to the playhead." });
      PropertyFloat("Duration", state.params.duration, {
        .min = ShotTemplates::MIN_DURATION, .max = MAX_SHOT_DURATION, .speed = 0.02f, .format = "%.2f", .unit = "s",
        .tooltip = "How long the shot lasts." });

      if (info.params != 0)
      {
        PropertySubHeading("Move");
        DrawAddShotMoveRows(state.params, info.params);
      }

      if (state.shot == ShotTemplate::StaticFollow)
      {
        PropertyStatus(nullptr, "The camera stands where the editor camera is when you press Create. Fly there first.",
          StatusKind::Info);
      }
      else if (state.shot == ShotTemplate::FlyTo)
      {
        PropertyStatus(nullptr, "Flies from where the editor camera is when you press Create to the view the camera "
          "bound in the Sequencer has at Start. Without such a camera it ends 5 m ahead of the editor camera.",
          StatusKind::Info);
      }

      if (!state.error.empty())
        PropertyStatus(nullptr, state.error.c_str(), StatusKind::Error);
      EndPropertyScope();

      const char* createReason = info.needsTarget && state.target == entt::null ? "Pick a Target first"
        : !state.newCamera && state.camera == entt::null ? "Pick a Camera, or turn on New Camera"
        : context.editorCamera == nullptr ? "The editor camera is not available"
        : nullptr;
      if (InlineButton("Create", {
        .icon = ICON_LC_CHECK,
        .tooltip = "Adds the shot, binds its camera in the Sequencer and selects the shot's first key",
        .disabledReason = createReason }))
      {
        const EditorCommands::AddShotResult result = AddShot(context, state.shot, state.params,
          info.needsTarget ? state.target : Entity(entt::null), state.newCamera ? Entity(entt::null) : state.camera);
        if (result.error.empty())
          state.open = false;
        else
          state.error = result.error;
      }
      ImGui::SameLine();
      if (InlineButton("Cancel", { .icon = ICON_LC_X }))
        state.open = false;
    }
  }

  uint64_t HashTrackMotionInputs(Scene& scene, Entity trackEntity)
  {
    MotionHasher hasher;
    hasher.Add(entt::to_integral(trackEntity));
    if (trackEntity == entt::null || !scene.GetRegistry().valid(trackEntity)
      || !scene.HasComponent<CameraTrackComponent>(trackEntity))
    {
      return hasher.value;
    }

    const auto& track = scene.GetComponent<CameraTrackComponent>(trackEntity);
    hasher.Add(track.keys.size());
    for (const CameraTrackKey& key : track.keys)
    {
      hasher.Add(key.time);
      hasher.Add(key.position);
      hasher.Add(key.rotation);
      hasher.Add(key.fov);
      hasher.Add(key.space);
      hasher.Add(key.interpOut);
      hasher.Add(key.easeIn);
      hasher.Add(key.easeOut);
    }
    hasher.Add(track.rotationMode);
    hasher.Add(track.aimTargetName);
    hasher.Add(track.followTargetName);
    hasher.Add(track.followRotation);
    hasher.Add(track.followSmoothing);
    hasher.Add(track.aimOffset);
    hasher.Add(track.aimScreenOffset);
    hasher.Add(track.aimSmoothing);
    if (scene.HasComponent<CameraComponent>(trackEntity))
      hasher.Add(scene.GetComponent<CameraComponent>(trackEntity).aspectRatio);

    // Names resolve to other entities after a rename or a delete
    hasher.Add(scene.GetStructureGeneration());
    const SequencePlayer::TrackTargets targets = SequencePlayer::FindTrackTargets(scene, trackEntity);
    HashTargetEntity(hasher, scene, targets.follow);
    HashTargetEntity(hasher, scene, targets.aim);
    return hasher.value;
  }

  void SampleTrackMotion(Scene& scene, Entity trackEntity, float start, float end, int32_t count,
    std::vector<MotionSample>& out)
  {
    out.clear();
    if (count <= 0 || !(end > start) || trackEntity == entt::null || !scene.GetRegistry().valid(trackEntity)
      || !scene.HasComponent<CameraTrackComponent>(trackEntity))
    {
      return;
    }

    const auto& track = scene.GetComponent<CameraTrackComponent>(trackEntity);
    // A held key jumps to the next one on purpose; a step across that cut is not motion
    std::vector<float> cuts;
    for (size_t i = 1; i < track.keys.size(); i++)
    {
      if (track.keys[i - 1].interpOut == CameraKeyInterp::Hold)
        cuts.push_back(track.keys[i].time);
    }

    const SequencePlayer::TrackTargets targets = SequencePlayer::FindTrackTargets(scene, trackEntity);
    const float step = std::min(MOTION_STEP, end - start);
    out.reserve(size_t(count));
    for (int32_t i = 0; i < count; i++)
    {
      const float t = count == 1 ? start : glm::mix(start, end, float(i) / float(count - 1));
      float a = glm::clamp(t - 0.5f * step, start, end - step);
      float b = a + step;
      for (float cut : cuts)
      {
        if (cut <= a || cut > b)
          continue;
        if (t < cut)
        {
          b = cut - 1e-4f;
          a = std::max(b - step, start);
        }
        else
        {
          a = cut;
          b = std::min(a + step, end);
        }
      }
      if (b - a <= 1e-5f)
      {
        out.push_back({ .time = t });
        continue;
      }

      CameraTrackPose from;
      CameraTrackPose to;
      if (!SequencePlayer::EvaluateTrackPose(scene, trackEntity, targets, a, from)
        || !SequencePlayer::EvaluateTrackPose(scene, trackEntity, targets, b, to))
      {
        out.clear();
        return;
      }

      const float angle = glm::length(CameraTrackDetail::RelativeLog(from.rotation, to.rotation));
      out.push_back({
        .time = t,
        .speed = glm::distance(from.position, to.position) / (b - a),
        .turnRate = glm::degrees(angle) / (b - a) });
    }
  }

  ShotViewPoses CaptureShotViews(EditorContext& context, float start)
  {
    ShotViewPoses views;
    if (context.scene == nullptr)
      return views;

    EditorCameraPose(context, views.editorCamera);

    const CameraTrackComponent* track = GetBoundTrack(context);
    if (track == nullptr || track->keys.empty()
      || !SequencePlayer::EvaluateTrackPose(*context.scene, context.sequencerTrack, start, views.destination))
    {
      views.destination = views.editorCamera;
      views.destination.position += views.editorCamera.rotation * glm::vec3(0.0f, 0.0f, -FLY_TO_FALLBACK_DISTANCE);
    }
    return views;
  }

  EditorCommands::AddShotResult AddShot(EditorContext& context, ShotTemplate shot, const ShotTemplateParams& params,
    Entity target, Entity camera)
  {
    EditorCommands::AddShotResult result;
    if (context.scene == nullptr)
    {
      result.error = "No scene";
      return result;
    }

    Scene& scene = *context.scene;
    const ShotViewPoses views = CaptureShotViews(context, params.start);
    result = EditorCommands::AddShot(scene, shot, params, target, camera, views);
    if (!result.error.empty())
    {
      ShowStatus(context, result.error.c_str());
      return result;
    }

    BindTrack(context, result.camera);
    // A selected entity with another track would take the binding back on the next frame
    if (context.selectedEntity != result.camera && HasTrack(scene, context.selectedEntity))
      context.SelectEntity(result.camera);
    SelectKey(context, result.firstKey);
    RevealTimelineEnd(context);
    SetPlayhead(context, params.start);

    const std::string cameraName = EditorCommands::GetEntityDisplayName(scene.GetRegistry(), result.camera);
    if (!result.warning.empty())
    {
      ShowStatus(context, result.warning.c_str());
    }
    else
    {
      char text[200];
      std::snprintf(text, sizeof(text), "Added %s to '%s': %d keys from %.2f to %.2f s",
        ShotTemplates::GetInfo(shot).name, cameraName.c_str(), int(result.keyCount),
        params.start, params.start + params.duration);
      ShowStatus(context, text);
    }
    YA_LOG_INFO("Editor", "Sequencer: added shot %s to '%s' at %.2f s (%d keys)", ShotTemplates::GetInfo(shot).name,
      cameraName.c_str(), params.start, int(result.keyCount));
    return result;
  }

  void DrawAddShotButton(EditorContext& context)
  {
    using namespace EditorWidgets;

    const char* reason = context.scene == nullptr ? "No scene"
      : context.editorCamera == nullptr ? "The editor camera is not available"
      : nullptr;
    if (InlineButton("Add Shot", {
      .icon = ICON_LC_FILM,
      .tooltip = "Adds a ready-made camera move (chase, orbit, wheel close-up...) as ordinary keys you can edit "
                 "afterwards. Pick one from the list, then set it up in the window that opens.",
      .disabledReason = reason }))
    {
      ImGui::OpenPopup("##AddShotMenu");
    }

    if (!ImGui::BeginPopup("##AddShotMenu"))
      return;

    int32_t picked = -1;
    for (size_t i = 0; i < size_t(ShotTemplate::Count); i++)
    {
      const ShotTemplateInfo& info = ShotTemplates::GetInfo(ShotTemplate(i));
      if (ImGui::MenuItem(info.name))
        picked = int32_t(i);
      ItemTooltip(info.tooltip);
    }
    ImGui::EndPopup();

    if (picked >= 0 && context.scene != nullptr)
      OpenAddShotWindow(context, ShotTemplate(picked));
  }

  void DrawAddShotWindow(EditorContext& context)
  {
    AddShotWindowState& state = s_AddShot;
    if (!state.open)
      return;
    if (context.scene == nullptr)
    {
      state.open = false;
      return;
    }

    const entt::registry& registry = context.scene->GetRegistry();
    if (state.target != entt::null && !registry.valid(state.target))
      state.target = entt::null;
    if (state.camera != entt::null && (!registry.valid(state.camera) || !registry.all_of<CameraComponent>(state.camera)))
      state.camera = entt::null;

    const float scale = EditorStyle::GetContentScale();
    // Zero height fits the rows, which change with the template
    ImGui::SetNextWindowSize(ImVec2(ADD_SHOT_WINDOW_WIDTH * scale, 0.0f), ImGuiCond_Always);
    if (state.appearing)
    {
      ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowFocus();
      state.appearing = false;
    }

    // A plain window rather than a modal, so the editor camera can still be flown to where the shot should start
    bool open = true;
    if (ImGui::Begin("Add Shot", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoResize))
      DrawAddShotContents(context);
    ImGui::End();
    if (!open)
      state.open = false;
  }

  void CloseAddShotWindow()
  {
    s_AddShot = AddShotWindowState {};
  }
}
