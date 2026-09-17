#include "Editor/Panels/ShotInspectorPanel.h"

#include <imgui.h>

#include "Editor/EditorContext.h"
#include "Editor/SequencerEditing.h"
#include "Editor/Utils/EditorIcons.h"
#include "Editor/Utils/EditorStyle.h"
#include "Editor/Utils/EditorWidgets.h"
#include "Scene/SequencePlayer.h"
#include "Utils/CameraOrientation.h"
#include "Utils/Log.h"

namespace YAEngine
{
  using namespace EditorWidgets;
  using SequencerEditing::KeyLowerBound;
  using SequencerEditing::KeyUpperBound;

  namespace
  {
    constexpr float DUPLICATE_OFFSET = 0.5f;
    constexpr float MIN_FOV_DEGREES = 10.0f;
    constexpr float MAX_FOV_DEGREES = 120.0f;
    constexpr float ROTATION_EPSILON = 1e-6f;

    constexpr float MAX_SPEED = 100.0f;
    constexpr float MS_TO_KMH = 3.6f;
    constexpr float MIN_TURN_RADIUS = 0.5f;
    constexpr float MAX_TURN_RADIUS = 100.0f;
    constexpr float DEFAULT_TURN_RADIUS = 5.0f;
    constexpr float MAX_STEERING_SMOOTHING = 20.0f;
    constexpr float DEFAULT_STEERING_SMOOTHING = 2.5f;
    // A drive ending faster than this stops visibly dead at the end of its path
    constexpr float STOP_SPEED_WARNING = 0.5f;
    // Where a point appended past the end of the path lands, and how far the view ray may reach
    constexpr float APPEND_SPACING = 5.0f;
    constexpr float MAX_VIEW_POINT_DISTANCE = 500.0f;

    constexpr float MAX_SMOOTHING_SECONDS = 5.0f;
    constexpr float DEFAULT_FOLLOW_SMOOTHING = 0.5f;
    constexpr float AIM_PRESET_EPSILON = 1e-3f;
    constexpr float NUDGE_METRES_PER_PIXEL = 0.01f;
    constexpr float NUDGE_DEGREES_PER_PIXEL = 0.1f;
    // Samples per second of shot for the Motion readout, and a cap for very long shots
    constexpr float MOTION_READOUT_RATE = 30.0f;
    constexpr int32_t MAX_MOTION_READOUT_SAMPLES = 1800;

    constexpr EnumOption ROTATION_MODES[] = {
      { .label = "Keyframed", .tooltip = "The camera takes the rotation stored in the keys" },
      { .label = "Aim At Entity", .tooltip = "The camera turns toward the Aim Target; the keyed rotation is used while no "
                                             "entity has that name" },
    };

    constexpr EnumOption FOLLOW_ROTATIONS[] = {
      { .label = "Full", .tooltip = "The camera turns exactly as the target turns" },
      { .label = "Smoothed", .tooltip = "The camera turns with the target's heading averaged over Follow Smoothing, so "
                                        "small steering wobbles do not shake the shot" },
      { .label = "Position only", .tooltip = "The camera moves with the target but never turns with it: the keys keep "
                                             "their direction in the world" },
    };

    enum class AimPreset : uint8_t { BodyCentre, LeftFrontWheel, RightFrontWheel, Nose, Tail, Custom, Count };

    constexpr EnumOption AIM_PRESETS[] = {
      { .label = "Body centre", .tooltip = "The middle of the car" },
      { .label = "Left front wheel", .tooltip = "The centre of the left front wheel" },
      { .label = "Right front wheel", .tooltip = "The centre of the right front wheel" },
      { .label = "Nose", .tooltip = "The front bumper" },
      { .label = "Tail", .tooltip = "The rear bumper" },
      { .label = "Custom", .tooltip = "Whatever Aim Offset holds" },
    };

    glm::vec3 AimPresetOffset(AimPreset preset)
    {
      const ShotSubject subject;
      switch (preset)
      {
        case AimPreset::LeftFrontWheel: return subject.frontWheel;
        case AimPreset::RightFrontWheel: return subject.frontWheel * glm::vec3(-1.0f, 1.0f, 1.0f);
        case AimPreset::Nose: return subject.nose;
        case AimPreset::Tail: return subject.tail;
        default: return subject.bodyCentre;
      }
    }

    AimPreset FindAimPreset(const glm::vec3& offset)
    {
      for (uint8_t i = 0; i < uint8_t(AimPreset::Custom); i++)
      {
        if (glm::all(glm::lessThanEqual(glm::abs(offset - AimPresetOffset(AimPreset(i))), glm::vec3(AIM_PRESET_EPSILON))))
          return AimPreset(i);
      }
      return AimPreset::Custom;
    }

    constexpr EnumOption KEY_SPACES[] = {
      { .label = "World", .tooltip = "The key is a fixed spot in the level" },
      { .label = "Relative to target", .tooltip = "The key rides along with the Follow Target, so the camera keeps its "
                                                  "place next to it" },
    };

    bool SameRotation(const glm::quat& a, const glm::quat& b)
    {
      return std::abs(a.x - b.x) <= ROTATION_EPSILON && std::abs(a.y - b.y) <= ROTATION_EPSILON
        && std::abs(a.z - b.z) <= ROTATION_EPSILON && std::abs(a.w - b.w) <= ROTATION_EPSILON;
    }

    // Stretches of the path bending tighter than the turn radius
    int CountTightBends(const MotionPathTable& table, float minTurnRadius)
    {
      float limit = 1.0f / std::max(minTurnRadius, 0.01f);
      int count = 0;
      bool inside = false;
      for (float curvature : table.curvature)
      {
        bool tight = std::abs(curvature) > limit;
        if (tight && !inside)
          count++;
        inside = tight;
      }
      return count;
    }

    // Width of an InlineButton showing both the icon and the label
    float ButtonWidth(const char* label, const char* icon)
    {
      const ImGuiStyle& style = ImGui::GetStyle();
      return ImGui::CalcTextSize(icon).x + style.ItemInnerSpacing.x + ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f;
    }

    void SameLineIfFits(float width)
    {
      ImGui::SameLine();
      if (ImGui::GetContentRegionAvail().x < width)
        ImGui::NewLine();
    }

    int InsertCameraKey(CameraTrackComponent& track, const CameraTrackKey& key)
    {
      auto it = std::upper_bound(track.keys.begin(), track.keys.end(), key.time,
        [](float time, const CameraTrackKey& other) { return time < other.time; });
      int index = int(it - track.keys.begin());
      track.keys.insert(it, key);
      return index;
    }

    // Warnings for a reference stored by name, shared by the follow and the aim target
    void NameLookupStatus(const std::string& name, Entity resolved, uint32_t matches, const char* missingTooltip,
      const char* ambiguousTooltip)
    {
      if (name.empty())
        return;

      if (resolved == entt::null)
      {
        const std::string missing = "No entity is named '" + name + "'";
        PropertyStatus(nullptr, missing.c_str(), StatusKind::Warning, missingTooltip);
      }
      else if (matches > 1)
      {
        const std::string ambiguous = std::to_string(matches) + " entities are named '" + name + "'";
        PropertyStatus(nullptr, ambiguous.c_str(), StatusKind::Warning, ambiguousTooltip);
      }
    }
  }

  void ShotInspectorPanel::OnSceneReady(EditorContext& context)
  {
    m_EulerTrack = entt::null;
    m_EulerKey = -1;
    m_Nudge = {};
    m_NudgeTrack = entt::null;
    m_NudgeKey = -1;
    m_MotionHash = 0;
    m_MotionSamples.clear();
  }

  void ShotInspectorPanel::OnRender(EditorContext& context)
  {
    if (!BeginPanel())
    {
      ImGui::End();
      return;
    }

    if (context.scene == nullptr)
    {
      ImGui::TextDisabled("No scene");
      ImGui::End();
      return;
    }

    SequencerEditing::ResolveBinding(context);
    SequencerEditing::HandleHotkeys(context);

    CameraTrackComponent* track = SequencerEditing::GetBoundTrack(context);
    MotionPathComponent* path = SequencerEditing::GetBoundPath(context);
    if (track == nullptr && path == nullptr)
    {
      ImGui::TextDisabled("Bind a camera track or a motion path in the Sequencer.");
      SequencerEditing::DrawAddShotButton(context);
      ImGui::End();
      return;
    }

    if (track != nullptr)
    {
      DrawShot(context, *track);
      DrawCameraKey(context, *track);
    }
    else
    {
      SequencerEditing::DrawAddShotButton(context);
    }
    if (path != nullptr)
      DrawCarDrive(context, *path);

    ImGui::End();
  }

  bool ShotInspectorPanel::IsAimOverridingRotation(EditorContext& context, const CameraTrackComponent& track)
  {
    return track.rotationMode == CameraTrackComponent::RotationMode::AimAt
      && m_AimTargetLookup.Resolve(*context.scene, track.aimTargetName) != entt::null;
  }

  void ShotInspectorPanel::DrawShot(EditorContext& context, CameraTrackComponent& track)
  {
    if (!BeginPropertyGroup("Shot", { .icon = ICON_LC_CLAPPERBOARD, .tooltip = "The camera track bound in the Sequencer and how it plays" }))
      return;

    Scene& scene = *context.scene;
    entt::registry& registry = scene.GetRegistry();
    const Entity trackEntity = context.sequencerTrack;

    const std::string cameraName = EditorCommands::GetEntityDisplayName(registry, trackEntity);
    PropertyReadOnly("Camera", cameraName.c_str(), { .tooltip = "Camera the shot plays through. The Sequencer binds another one." });

    char range[64];
    if (track.keys.empty())
      std::snprintf(range, sizeof(range), "no keys");
    else
      std::snprintf(range, sizeof(range), "%.2f - %.2f s, %zu %s", SequencePlayer::ShotStart(track),
        SequencePlayer::ShotEnd(track), track.keys.size(), track.keys.size() == 1 ? "key" : "keys");
    PropertyReadOnly("Range", range, { .mono = true, .tooltip = "The shot runs from its first key to its last" });

    SuspendPropertyGrid();
    SequencerEditing::DrawAddShotButton(context);

    using SequencerEditing::RetimePace;
    const std::string steadyReason = SequencerEditing::GetRetimeUnavailableReason(context, RetimePace::Steady);
    SameLineIfFits(ButtonWidth("Even Out Speed", ICON_LC_GAUGE));
    if (InlineButton("Even Out Speed", {
      .icon = ICON_LC_GAUGE,
      .tooltip = "The camera moves at a steady pace between the selected keys; relative keys are measured around the "
                 "car, so the car's own braking is kept. Shift+click keys on the Camera lane to pick them; without a "
                 "selection the whole track is used. The first and last key keep their times, and so do Hold stretches.",
      .disabledReason = steadyReason.empty() ? nullptr : steadyReason.c_str() }))
    {
      SequencerEditing::RetimeKeys(context, RetimePace::Steady);
    }

    const std::string carReason = SequencerEditing::GetRetimeUnavailableReason(context, RetimePace::Car);
    SameLineIfFits(ButtonWidth("Match Car Pace", ICON_LC_CAR));
    if (InlineButton("Match Car Pace", {
      .icon = ICON_LC_CAR,
      .tooltip = "The camera speeds up and slows down together with the car between the selected keys. The car is "
                 "the Follow Target, or the motion path bound in the Sequencer. Without a selection the whole track "
                 "is used; the first and last key keep their times, and so do Hold stretches.",
      .disabledReason = carReason.empty() ? nullptr : carReason.c_str() }))
    {
      SequencerEditing::RetimeKeys(context, RetimePace::Car);
    }

    DrawFollow(context, track);
    DrawAim(context, track);
    DrawMotion(context, track);

    PropertySubHeading("Playback");
    PropertyBool("Reset PostFX On Start", track.resetPostFXOnStart, {
      .defaultValue = true,
      .tooltip = "Clears the temporal history and the auto exposure when playback starts, so the shot does not inherit "
                 "them from the previous viewpoint" });

    EndPropertyGroup();
  }

  void ShotInspectorPanel::DrawFollow(EditorContext& context, CameraTrackComponent& track)
  {
    PropertySubHeading("Follow");

    Scene& scene = *context.scene;
    entt::registry& registry = scene.GetRegistry();
    const Entity trackEntity = context.sequencerTrack;

    // Stored as a name, resolved to an entity only for the picker, like the Aim Target
    Entity target = m_FollowTargetLookup.Resolve(scene, track.followTargetName);
    const PropertyEdit targetEdit = PropertyEntity("Follow Target", target, scene, {
      .allowNone = true,
      .uniqueNames = true,
      .filter = [trackEntity](Entity candidate) { return candidate != trackEntity; },
      .tooltip = "Entity the camera rides along with, usually the car. Keys set to Relative to target move with it; "
                 "switching to another entity carries those keys over to it unchanged. Stored by its name, so renaming "
                 "that entity breaks the link, and an entity whose name another entity shares cannot be picked." });
    if (targetEdit.changed)
    {
      const Name* name = target != entt::null ? registry.try_get<Name>(target) : nullptr;
      track.followTargetName = name != nullptr ? *name : std::string();
      target = m_FollowTargetLookup.Resolve(scene, track.followTargetName);
      SequencerEditing::Repose(context);
    }

    NameLookupStatus(track.followTargetName, target, m_FollowTargetLookup.GetMatchCount(),
      "Keys relative to the target are read as world positions while the name matches no entity. Pick a target or None.",
      "The camera follows whichever of them the scene lists first, which can change as entities are added or removed. "
      "Rename all but one of them, or pick another target.");

    int32_t targetKeys = 0;
    for (const CameraTrackKey& key : track.keys)
      targetKeys += key.space == CameraKeySpace::Target ? 1 : 0;
    if (targetKeys > 0 && SequencerEditing::GetTargetSpaceUnavailableReason(context) != nullptr)
    {
      char text[128];
      std::snprintf(text, sizeof(text), "%d %s relative to a target, but there is none to follow", targetKeys,
        targetKeys == 1 ? "key is" : "keys are");
      PropertyStatus(nullptr, text, StatusKind::Warning,
        "Those keys are read as world positions, so the camera ends up somewhere near the world origin. Pick a Follow "
        "Target, or switch the keys to World.");
    }

    const bool aiming = track.rotationMode == CameraTrackComponent::RotationMode::AimAt;
    const bool following = !track.followTargetName.empty();
    if (PropertyEnum("Follow Rotation", track.followRotation, FOLLOW_ROTATIONS, {
      .defaultValue = 0,
      .tooltip = "How the camera turns with the target. The Aim Target's aim point turns the same way.",
      .disabledReason = following || aiming ? nullptr : "Pick a Follow Target first" }).changed)
    {
      SequencerEditing::Repose(context);
    }

    PushDependency(track.followRotation == CameraTrackComponent::FollowRotation::Smoothed,
      "Requires Follow Rotation: Smoothed");
    if (PropertyFloat("Follow Smoothing", track.followSmoothing, {
      .min = 0.0f, .max = MAX_SMOOTHING_SECONDS, .speed = 0.01f, .format = "%.2f", .unit = "s",
      .defaultValue = DEFAULT_FOLLOW_SMOOTHING,
      .tooltip = "How many seconds of the target's heading are averaged. More gives a calmer camera that lags behind "
                 "sharp turns; 0 turns exactly with the target.",
      .disabledReason = following || aiming ? nullptr : "Pick a Follow Target first" }).changed)
    {
      track.followSmoothing = glm::clamp(track.followSmoothing, 0.0f, MAX_SMOOTHING_SECONDS);
      SequencerEditing::Repose(context);
    }
    PopDependency();
  }

  void ShotInspectorPanel::DrawAim(EditorContext& context, CameraTrackComponent& track)
  {
    PropertySubHeading("Aim");

    Scene& scene = *context.scene;
    entt::registry& registry = scene.GetRegistry();
    const Entity trackEntity = context.sequencerTrack;

    if (PropertyEnum("Rotation Mode", track.rotationMode, ROTATION_MODES, {
      .defaultValue = 0,
      .tooltip = "Where the camera's rotation comes from while the track plays" }).changed)
    {
      SequencerEditing::Repose(context);
    }

    const bool aiming = track.rotationMode == CameraTrackComponent::RotationMode::AimAt;
    PushDependency(aiming, "Requires Rotation Mode: Aim At Entity");

    // Stored as a name, resolved to an entity only for the picker, so the scene format stays as it was
    Entity target = m_AimTargetLookup.Resolve(scene, track.aimTargetName);
    const PropertyEdit targetEdit = PropertyEntity("Aim Target", target, scene, {
      .allowNone = true,
      .uniqueNames = true,
      .filter = [trackEntity](Entity candidate) { return candidate != trackEntity; },
      .tooltip = "Entity the camera turns toward. Stored by its name, so renaming that entity breaks the link, and an "
                 "entity whose name another entity shares cannot be picked." });
    if (targetEdit.changed)
    {
      const Name* name = target != entt::null ? registry.try_get<Name>(target) : nullptr;
      track.aimTargetName = name != nullptr ? *name : std::string();
      target = m_AimTargetLookup.Resolve(scene, track.aimTargetName);
      SequencerEditing::Repose(context);
    }

    if (aiming)
    {
      NameLookupStatus(track.aimTargetName, target, m_AimTargetLookup.GetMatchCount(),
        "The camera falls back to the keyed rotation while the name matches no entity. Pick a target or None.",
        "The camera aims at whichever of them the scene lists first, which can change as entities are added or "
        "removed. Rename all but one of them, or pick another target.");
    }

    int32_t preset = int32_t(FindAimPreset(track.aimOffset));
    if (PropertyEnum("Aim Point", preset, AIM_PRESETS, {
      .tooltip = "Which part of the target the camera looks at. The points fit the demo car; Custom keeps whatever "
                 "Aim Offset holds." }).changed && preset != int32_t(AimPreset::Custom))
    {
      track.aimOffset = AimPresetOffset(AimPreset(preset));
      SequencerEditing::Repose(context);
    }

    if (PropertyVec3("Aim Offset", track.aimOffset, {
      .speed = 0.01f, .format = "%.2f", .unit = "m",
      .defaultValue = glm::vec3(0.0f),
      .tooltip = "The point the camera looks at, measured from the target's origin (a car's rear axle, on the ground): "
                 "X to its left, Y up, Z forward. Picking an Aim Point fills it in." }).changed)
    {
      SequencerEditing::Repose(context);
    }

    if (PropertyVec2("Screen Offset", track.aimScreenOffset, {
      .speed = 0.005f, .min = -1.0f, .max = 1.0f, .format = "%.2f",
      .defaultValue = glm::vec2(0.0f),
      .tooltip = "Where the aim point sits in the picture. 0, 0 is the centre; X -1 is the left edge and +1 the right "
                 "edge, Y -1 the bottom and +1 the top. About +-0.33 puts it on a rule-of-thirds line." }).changed)
    {
      SequencerEditing::Repose(context);
    }

    if (PropertyFloat("Aim Smoothing", track.aimSmoothing, {
      .min = 0.0f, .max = MAX_SMOOTHING_SECONDS, .speed = 0.01f, .format = "%.2f", .unit = "s",
      .defaultValue = 0.0f,
      .tooltip = "Averages where the aim point is over this many seconds, so the camera does not twitch with every "
                 "bump. 0 follows it exactly." }).changed)
    {
      track.aimSmoothing = glm::clamp(track.aimSmoothing, 0.0f, MAX_SMOOTHING_SECONDS);
      SequencerEditing::Repose(context);
    }

    PopDependency();
  }

  void ShotInspectorPanel::DrawMotion(EditorContext& context, const CameraTrackComponent& track)
  {
    PropertySubHeading("Motion");

    if (track.keys.size() < 2)
    {
      PropertyStatus(nullptr, "Add at least two keys to see how fast the camera moves.");
      return;
    }

    Scene& scene = *context.scene;
    const float start = SequencePlayer::ShotStart(track);
    const float end = SequencePlayer::ShotEnd(track);
    const int32_t count = glm::clamp(int32_t((end - start) * MOTION_READOUT_RATE) + 1, 2, MAX_MOTION_READOUT_SAMPLES);
    const uint64_t hash = SequencerEditing::HashTrackMotionInputs(scene, context.sequencerTrack) ^ uint64_t(count);
    if (hash != m_MotionHash || m_MotionSamples.empty())
    {
      m_MotionHash = hash;
      SequencerEditing::SampleTrackMotion(scene, context.sequencerTrack, start, end, count, m_MotionSamples);
      m_TopSpeed = {};
      m_TopTurn = {};
      for (const SequencerEditing::MotionSample& sample : m_MotionSamples)
      {
        if (sample.speed > m_TopSpeed.speed)
          m_TopSpeed = sample;
        if (sample.turnRate > m_TopTurn.turnRate)
          m_TopTurn = sample;
      }
    }
    if (m_MotionSamples.empty())
      return;

    auto jumpRow = [&context](const char* label, const char* text, float time, bool warn, const char* tooltip) {
      if (!BeginPropertyRow(label, { .tooltip = tooltip }))
        return;
      if (warn)
        ImGui::PushStyleColor(ImGuiCol_Text, ToImGuiColor(EditorStyle::GetTheme().warning));
      if (ImGui::Selectable(text))
        SequencerEditing::SetPlayhead(context, time);
      if (warn)
        ImGui::PopStyleColor();
      EndPropertyRow();
    };

    const bool fast = m_TopSpeed.speed > SequencerEditing::MOTION_SPEED_WARNING;
    const bool turning = m_TopTurn.turnRate > SequencerEditing::MOTION_TURN_WARNING;

    char speedText[64];
    std::snprintf(speedText, sizeof(speedText), "%.1f m/s at %.2f s###Jump", m_TopSpeed.speed, m_TopSpeed.time);
    jumpRow("Top Speed", speedText, m_TopSpeed.time, fast,
      "The fastest the camera moves through the world during the shot. Click to move the playhead there.");

    char turnText[64];
    std::snprintf(turnText, sizeof(turnText), "%.0f deg/s at %.2f s###Jump", m_TopTurn.turnRate, m_TopTurn.time);
    jumpRow("Fastest Turn", turnText, m_TopTurn.time, turning,
      "The fastest the camera turns during the shot. Click to move the playhead there.");

    if (fast || turning)
    {
      char text[160];
      std::snprintf(text, sizeof(text), "Faster than %s%s%s: viewers find this hard to follow",
        fast ? "20 m/s" : "", fast && turning ? " and " : "", turning ? "90 deg/s" : "");
      PropertyStatus(nullptr, text, StatusKind::Warning,
        "Give the move more time, spread it over more keys, or raise Follow Smoothing. The red stretches on the "
        "Sequencer's Camera lane show where.");
    }
  }

  void ShotInspectorPanel::DrawCameraKey(EditorContext& context, CameraTrackComponent& track)
  {
    if (!BeginPropertyGroup("Camera Key", { .icon = ICON_LC_KEY, .tooltip = "The camera key selected in the Sequencer or in the viewport" }))
      return;

    int index = context.sequencerSelectedKey;
    if (index < 0 || index >= int(track.keys.size()))
    {
      PropertyStatus(nullptr, "Click a key on the Camera lane to select it, then drag it to retime it: Ctrl snaps, Esc or a "
        "right click cancels. Shift+click another key selects the keys in between; drag inside that range to move it or "
        "its edges to stretch it, and Esc clears it. Ctrl+wheel zooms; "
        "Shift+wheel, Shift-drag on empty space or middle-drag pans. With the Sequencer or this "
        "panel focused, [ and ] step between keys, the arrows step the playhead (Shift: 1 s), Space plays, P pilots the "
        "camera and K adds a key (in pilot mode: sets the key at the playhead to the flown view).");
      EndPropertyGroup();
      return;
    }

    float lower = KeyLowerBound(track.keys, index);
    float upper = KeyUpperBound(track.keys, index);
    KeyAction action = KeyAction::None;

    {
      CameraTrackKey& key = track.keys[index];

      int rangeFirst = -1;
      int rangeLast = -1;
      if (SequencerEditing::GetKeyRange(context, rangeFirst, rangeLast))
      {
        const float rangeStart = track.keys[size_t(rangeFirst)].time;
        const float rangeEnd = track.keys[size_t(rangeLast)].time;
        char selection[80];
        std::snprintf(selection, sizeof(selection), "Keys %d-%d: %.2f-%.2f s (%.2f s)", rangeFirst + 1, rangeLast + 1,
          rangeStart, rangeEnd, rangeEnd - rangeStart);
        PropertyReadOnly("Selection", selection, { .mono = true,
          .tooltip = "Even Out Speed and Match Car Pace work on these keys. The rows below edit only the key clicked "
                     "last. Shift+click on the Camera lane changes the range; a plain click or Esc goes back to one key." });
        PropertyStatus(nullptr, "Drag the range's band on the Camera lane to move it, or a band edge to stretch it",
          StatusKind::Neutral, "Moving keeps the keys around the range where they are. The right edge scales the range "
          "from its start and shifts the later keys with it; the left edge scales it from its end, up to the key "
          "before. Ctrl snaps, Esc or a right click cancels.");
      }

      char position[32];
      std::snprintf(position, sizeof(position), "%d of %d", index + 1, int(track.keys.size()));
      PropertyReadOnly("Index", position, { .mono = true, .tooltip = "Position of the key on the track, counted from 1" });

      float time = key.time;
      if (PropertyFloat("Time", time, {
        .min = lower, .max = upper, .speed = 0.02f, .format = "%.3f", .unit = "s",
        .tooltip = "Seconds on the timeline. Kept between the neighbouring keys, so the key order never changes. The "
                   "shot runs from the first key to the last." }).changed)
      {
        // Explicit, since bounds that meet leave the row unclamped
        key.time = glm::clamp(time, lower, upper);
        SequencerEditing::SetPlayhead(context, key.time);
      }
    }

    DrawKeyNudge(context, track, index);
    DrawKeyBlend(context, track, index);

    SuspendPropertyGrid();
    if (InlineButton("Update From View", {
      .icon = ICON_LC_CAMERA,
      .tooltip = "Replaces the key's position and rotation with the editor camera's, and its fov with the track camera's" }))
    {
      action = KeyAction::UpdateFromView;
    }
    SameLineIfFits(ButtonWidth("Duplicate", ICON_LC_COPY));
    if (InlineButton("Duplicate", { .icon = ICON_LC_COPY, .tooltip = "Copies the key a little later on the track" }))
      action = KeyAction::Duplicate;
    SameLineIfFits(ButtonWidth("Delete", ICON_LC_TRASH_2));
    if (InlineButton("Delete", { .icon = ICON_LC_TRASH_2 }))
      action = KeyAction::Delete;

    DrawKeyAdvanced(context, track, index);

    // Applied after every row is drawn: inserting or erasing reallocates
    switch (action)
    {
      case KeyAction::UpdateFromView:
        SequencerEditing::UpdateKeyFromView(context, index);
        break;
      case KeyAction::Duplicate:
      {
        CameraTrackKey copy = track.keys[index];
        float offset = DUPLICATE_OFFSET;
        if (index + 1 < int(track.keys.size()))
          offset = std::min(offset, (track.keys[index + 1].time - copy.time) * 0.5f);
        copy.time += std::max(offset, SequencerEditing::MIN_KEY_GAP);
        SequencerEditing::SelectKey(context, InsertCameraKey(track, copy));
        SequencerEditing::SetPlayhead(context, copy.time);
        break;
      }
      case KeyAction::Delete:
        track.keys.erase(track.keys.begin() + index);
        SequencerEditing::SelectKey(context, -1);
        SequencerEditing::Repose(context);
        break;
      case KeyAction::None:
        break;
    }

    EndPropertyGroup();
  }

  void ShotInspectorPanel::DrawKeyNudge(EditorContext& context, CameraTrackComponent& track, int index)
  {
    PropertySubHeading("Nudge");

    const Entity trackEntity = context.sequencerTrack;
    // A field let go of, or another key, starts from 0 again
    if (m_NudgeTrack != trackEntity || m_NudgeKey != index || !ImGui::IsAnyItemActive())
      m_Nudge = {};
    m_NudgeTrack = trackEntity;
    m_NudgeKey = index;

    CameraTrackKey& key = track.keys[index];
    const char* rotationReason = IsAimOverridingRotation(context, track)
      ? "The camera turns toward the Aim Target, so the key's own direction is not used"
      : nullptr;
    bool edited = false;

    auto nudge = [&](Nudge field, const char* label, bool degrees, const char* tooltip, const char* reason,
      const std::function<void(float)>& apply)
    {
      float& shown = m_Nudge[size_t(field)];
      float value = shown;
      const PropertyEdit edit = PropertyFloat(label, value, {
        .speed = degrees ? NUDGE_DEGREES_PER_PIXEL : NUDGE_METRES_PER_PIXEL,
        .format = degrees ? "%+.1f" : "%+.2f",
        .unit = degrees ? "deg" : "m",
        .tooltip = tooltip,
        .disabledReason = reason });
      if (edit.changed)
      {
        const float delta = value - shown;
        shown = value;
        if (delta != 0.0f && std::isfinite(delta))
        {
          apply(degrees ? glm::radians(delta) : delta);
          edited = true;
        }
      }
      if (edit.committed)
        shown = 0.0f;
    };

    const glm::vec3 forward = key.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec3 right = key.rotation * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);

    nudge(Nudge::Dolly, "Dolly", false,
      "Drag to move the camera forward (right) or back (left) along the way it looks. Goes back to 0 when you let go; "
      "the key keeps the move.", nullptr,
      [&key, forward](float d) { key.position += forward * d; });
    nudge(Nudge::Truck, "Truck", false,
      "Drag to move the camera sideways, to its right or left. Goes back to 0 when you let go.", nullptr,
      [&key, right](float d) { key.position += right * d; });
    nudge(Nudge::Pedestal, "Pedestal", false,
      "Drag to move the camera straight up or down. Goes back to 0 when you let go.", nullptr,
      [&key, up](float d) { key.position += up * d; });
    nudge(Nudge::Pan, "Pan", true,
      "Drag to turn the camera left (right drag) or right (left drag) around the vertical. Goes back to 0 when you "
      "let go.", rotationReason,
      [&key, up](float r) { key.rotation = glm::normalize(glm::angleAxis(r, up) * key.rotation); });
    nudge(Nudge::Tilt, "Tilt", true,
      "Drag to tip the camera up (right drag) or down (left drag). Goes back to 0 when you let go.", rotationReason,
      [&key](float r) { key.rotation = glm::normalize(key.rotation * glm::angleAxis(r, glm::vec3(1.0f, 0.0f, 0.0f))); });
    nudge(Nudge::Roll, "Roll", true,
      "Drag to lean the picture around the way the camera looks. Goes back to 0 when you let go.", rotationReason,
      [&key](float r) { key.rotation = glm::normalize(key.rotation * glm::angleAxis(r, glm::vec3(0.0f, 0.0f, 1.0f))); });

    float fovDegrees = glm::degrees(key.fov);
    if (PropertyFloat("FOV", fovDegrees, {
      .min = MIN_FOV_DEGREES, .max = MAX_FOV_DEGREES, .speed = 0.25f, .format = "%.1f", .unit = "deg",
      .tooltip = "Vertical field of view at this key, eased toward the neighbouring keys in between. Smaller zooms in." }).changed)
    {
      key.fov = glm::radians(glm::clamp(fovDegrees, MIN_FOV_DEGREES, MAX_FOV_DEGREES));
      edited = true;
    }

    if (edited)
      SequencerEditing::KeyEdited(context, index);
  }

  void ShotInspectorPanel::DrawKeyBlend(EditorContext& context, CameraTrackComponent& track, int index)
  {
    PropertySubHeading("Blend");

    const int count = int(track.keys.size());
    int32_t space = int32_t(track.keys[index].space);
    const char* targetReason = SequencerEditing::GetTargetSpaceUnavailableReason(context);
    if (PropertyEnum("Space", space, KEY_SPACES, {
      .tooltip = "World: the key is a fixed spot in the level. Relative to target: the key rides along with the Follow "
                 "Target, so the camera keeps its place next to the car. Switching keeps the camera where it is at this "
                 "key's time.",
      .disabledReason = track.keys[index].space == CameraKeySpace::World ? targetReason : nullptr }).changed)
    {
      SequencerEditing::SetKeySpace(context, index, CameraKeySpace(space));
    }

    CameraTrackKey& key = track.keys[index];
    const bool last = index + 1 >= count;
    const bool orbitPossible = !last && key.space == CameraKeySpace::Target
      && track.keys[size_t(index) + 1].space == CameraKeySpace::Target && targetReason == nullptr;
    const char* orbitReason = last ? "The last key has no movement after it"
      : targetReason != nullptr ? targetReason
      : !orbitPossible ? "Orbit needs this key and the next one to be Relative to target"
      : nullptr;

    const EnumOption interpolations[] = {
      { .label = "Smooth", .tooltip = "Eases through the keys on a smooth curve" },
      { .label = "Linear", .tooltip = "Moves in a straight line at an even pace" },
      { .label = "Hold", .tooltip = "Stays at this key, then jumps to the next one: a cut" },
      { .label = "Orbit", .disabledReason = orbitReason,
        .tooltip = "Circles around the target to the next key, turning with it" },
    };
    if (PropertyEnum("Interpolation", key.interpOut, interpolations, {
      .defaultValue = 0,
      .tooltip = "How the camera moves from this key to the next one.",
      .disabledReason = last ? "The last key has no movement after it" : nullptr }).changed)
    {
      SequencerEditing::KeyEdited(context, index);
    }
    if (key.interpOut == CameraKeyInterp::Orbit && orbitReason != nullptr)
    {
      PropertyStatus(nullptr, "This key orbits, but it moves smoothly instead", StatusKind::Warning, orbitReason);
    }

    if (PropertyFloat("Ease In", key.easeIn, {
      .min = 0.0f, .max = 1.0f, .format = "%.2f", .slider = true,
      .defaultValue = 1.0f,
      .tooltip = "How the camera arrives at this key. 0 = the camera comes to rest at this key; 1 = it passes through "
                 "at full pace.",
      .disabledReason = index == 0 ? "The first key has no movement arriving at it" : nullptr }).changed)
    {
      key.easeIn = glm::clamp(key.easeIn, 0.0f, 1.0f);
      SequencerEditing::KeyEdited(context, index);
    }

    if (PropertyFloat("Ease Out", key.easeOut, {
      .min = 0.0f, .max = 1.0f, .format = "%.2f", .slider = true,
      .defaultValue = 1.0f,
      .tooltip = "How the camera leaves this key. 0 = the camera starts moving from rest at this key; 1 = it leaves at "
                 "full pace.",
      .disabledReason = last ? "The last key has no movement after it" : nullptr }).changed)
    {
      key.easeOut = glm::clamp(key.easeOut, 0.0f, 1.0f);
      SequencerEditing::KeyEdited(context, index);
    }
  }

  void ShotInspectorPanel::DrawKeyAdvanced(EditorContext& context, CameraTrackComponent& track, int index)
  {
    if (!BeginPropertyGroup("Advanced", { .defaultOpen = false,
      .tooltip = "The key's raw position and rotation numbers" }))
    {
      return;
    }

    const Entity trackEntity = context.sequencerTrack;
    CameraTrackKey& key = track.keys[index];
    const bool relative = key.space == CameraKeySpace::Target;

    std::string spaceText = relative
      ? "Relative to '" + track.followTargetName + "': X to its left, Y up, Z forward, from its origin (a car's rear axle)"
      : std::string("World coordinates");
    PropertyStatus(nullptr, spaceText.c_str(), StatusKind::Info);

    if (PropertyVec3("Position", key.position, {
      .speed = 0.05f, .format = "%.2f", .unit = "m",
      .tooltip = relative
        ? "Camera position at this key, measured from the Follow Target: X to its left, Y up, Z forward."
        : "Camera position at this key in world coordinates." }).changed)
    {
      SequencerEditing::KeyEdited(context, index);
    }

    const bool cached = m_EulerTrack == trackEntity && m_EulerKey == index && SameRotation(m_EulerRotation, key.rotation);
    glm::vec3 angles = cached ? m_EulerDegrees : glm::degrees(YawPitchRollFromRotation(key.rotation));
    if (PropertyVec3("Rotation", angles, {
      .speed = 0.5f, .format = "%.1f", .unit = "deg", .componentLabels = { "Yaw", "Pitch", "Roll" },
      .tooltip = relative
        ? "Camera rotation at this key, relative to the Follow Target's heading: yaw about the vertical (180 looks the "
          "way the target drives), then pitch, then roll. With Rotation Mode Aim At "
          "Entity it is used only while the Aim Target matches no entity."
        : "World-space camera rotation at this key: yaw about world up, then pitch, then roll about the view direction. "
          "With Rotation Mode Aim At Entity it is used only while the Aim Target matches no entity." }).changed)
    {
      key.rotation = MakeYawPitchRollRotation(glm::radians(angles));
      SequencerEditing::KeyEdited(context, index);
    }
    m_EulerTrack = trackEntity;
    m_EulerKey = index;
    m_EulerRotation = key.rotation;
    m_EulerDegrees = angles;

    EndPropertyGroup();
  }

  void ShotInspectorPanel::DrawCarDrive(EditorContext& context, MotionPathComponent& path)
  {
    if (!BeginPropertyGroup("Car Drive", { .icon = ICON_LC_CAR, .tooltip = "The motion path bound in the Sequencer: its selected speed key and its curve" }))
      return;

    DrawSpeedKey(context, path);
    DrawPath(context, path);
    DrawPoints(context, path);

    EndPropertyGroup();
  }

  void ShotInspectorPanel::DrawSpeedKey(EditorContext& context, MotionPathComponent& path)
  {
    PropertySubHeading("Speed Key");

    int index = context.sequencerSelectedSpeedKey;
    if (index < 0 || index >= int(path.speedKeys.size()))
    {
      PropertyStatus(nullptr, "Click a key on the Speed lane to select it, then drag it to retime it. Shift+click another "
        "key selects the keys between. Add Speed Key places one at the playhead.");
      return;
    }

    float lower = KeyLowerBound(path.speedKeys, index);
    float upper = KeyUpperBound(path.speedKeys, index);
    bool erase = false;

    {
      MotionSpeedKey& key = path.speedKeys[index];

      int rangeFirst = -1;
      int rangeLast = -1;
      if (SequencerEditing::GetSpeedKeyRange(context, rangeFirst, rangeLast))
      {
        const float rangeStart = path.speedKeys[size_t(rangeFirst)].time;
        const float rangeEnd = path.speedKeys[size_t(rangeLast)].time;
        char selection[80];
        std::snprintf(selection, sizeof(selection), "Keys %d-%d: %.2f-%.2f s (%.2f s)", rangeFirst + 1, rangeLast + 1,
          rangeStart, rangeEnd, rangeEnd - rangeStart);
        PropertyReadOnly("Selection##SpeedRange", selection, { .mono = true,
          .tooltip = "The rows below edit only the key clicked last. Shift+click on the Speed lane changes the range; "
                     "a plain click or Esc goes back to one key." });
        PropertyStatus(nullptr, "Drag the range's band on the Speed lane to move it, or a band edge to slow it down or "
          "speed it up", StatusKind::Neutral, "The car keeps driving the same road: the speeds of the range are "
          "rescaled to fit the new timing, and the drive after the range plays as before, only earlier or later. The "
          "right edge keeps the drive up to the range's first key and shifts the later keys; the left edge keeps the "
          "drive from the range's last key on. Moving keeps the keys around the range where they are. Ctrl snaps, Esc "
          "or a right click cancels.");
      }

      char position[32];
      std::snprintf(position, sizeof(position), "%d of %d", index + 1, int(path.speedKeys.size()));
      PropertyReadOnly("Index", position, { .mono = true, .tooltip = "Position of the key on the lane, counted from 1" });

      float time = key.time;
      if (PropertyFloat("Time", time, {
        .min = lower, .max = upper, .speed = 0.02f, .format = "%.3f", .unit = "s",
        .tooltip = "Seconds on the timeline. Kept between the neighbouring keys. Before the first key the entity waits "
                   "at the start of the path." }).changed)
      {
        key.time = glm::clamp(time, lower, upper);
        SequencerEditing::SetPlayhead(context, key.time);
      }

      float speed = key.speed;
      if (PropertyFloat("Speed", speed, {
        .min = 0.0f, .max = MAX_SPEED, .speed = 0.05f, .format = "%.2f", .unit = "m/s",
        .tooltip = "Speed at this key, eased toward the neighbouring keys in between. After the last key it holds. A "
                   "drive never goes backwards." }).changed)
      {
        key.speed = glm::clamp(speed, 0.0f, MAX_SPEED);
        SequencerEditing::Repose(context);
      }

      char kmh[32];
      std::snprintf(kmh, sizeof(kmh), "%.1f km/h", key.speed * MS_TO_KMH);
      PropertyReadOnly("In km/h", kmh, { .mono = true });

      char distance[32];
      std::snprintf(distance, sizeof(distance), "%.2f m", EvaluateMotionTiming(path.speedKeys, key.time).distance);
      PropertyReadOnly("Distance", distance, { .mono = true, .tooltip = "How far along the path the entity is at this key" });

      SuspendPropertyGrid();
      if (InlineButton("Delete Speed Key", { .icon = ICON_LC_TRASH_2 }))
        erase = true;
    }

    if (erase)
    {
      path.speedKeys.erase(path.speedKeys.begin() + index);
      SequencerEditing::SelectSpeedKey(context, -1);
      SequencerEditing::Repose(context);
    }
  }

  void ShotInspectorPanel::DrawPath(EditorContext& context, MotionPathComponent& path)
  {
    PropertySubHeading("Path");

    SequencePlayer* player = context.sequencePlayer;
    // Only a running session is re-posed by curve edits: a stopped one must not start just
    // because a point moved
    const bool sessionActive = player != nullptr && player->IsActive();

    {
      const MotionPathTable& table = path.GetTable();

      char summary[64];
      std::snprintf(summary, sizeof(summary), "%zu points, %.1f m", path.points.size(), table.length);
      PropertyReadOnly("Curve", summary, { .mono = true, .tooltip = "Control points and the length of the curve through them" });

      char duration[32];
      std::snprintf(duration, sizeof(duration), "%.2f s", MotionPathDuration(table, path.speedKeys));
      PropertyReadOnly("Drive Ends", duration, { .mono = true,
        .tooltip = "Timeline time the drive is over: the end of the path or the last speed key, whichever comes later" });

      if (path.points.size() < 2)
      {
        PropertyStatus(nullptr, "Add at least two points: the entity drives only along a curve", StatusKind::Warning);
      }
      else
      {
        int tight = CountTightBends(table, path.minTurnRadius);
        if (tight > 0)
        {
          char text[96];
          std::snprintf(text, sizeof(text), "%d %s tighter than %.1f m", tight, tight == 1 ? "bend" : "bends",
            path.minTurnRadius);
          PropertyStatus(nullptr, text, StatusKind::Warning,
            "Drawn red in the viewport. A car steers no further than its maximum steering angle, so its front wheels "
            "stop matching the curve there.");
        }

        if (path.speedKeys.empty())
        {
          PropertyStatus(nullptr, "Add speed keys: without them the entity waits at the start", StatusKind::Info);
        }
        else
        {
          float end = MotionTimeAtDistance(path.speedKeys, table.length);
          float speedAtEnd = std::isfinite(end) ? EvaluateMotionTiming(path.speedKeys, end).speed : 0.0f;
          if (!std::isfinite(end))
          {
            PropertyStatus(nullptr, "The drive stops before the end of the path", StatusKind::Info);
          }
          else if (speedAtEnd > STOP_SPEED_WARNING)
          {
            char text[128];
            std::snprintf(text, sizeof(text), "The path ends at %.2f s at %.1f m/s", end, speedAtEnd);
            PropertyStatus(nullptr, text, StatusKind::Warning,
              "The entity stops dead there. Slow it down with a speed key before the end, or extend the path.");
          }
        }
      }
    }

    if (PropertyFloat("Min Turn Radius", path.minTurnRadius, {
      .min = MIN_TURN_RADIUS, .max = MAX_TURN_RADIUS, .speed = 0.05f, .format = "%.1f", .unit = "m",
      .defaultValue = DEFAULT_TURN_RADIUS,
      .tooltip = "Bends tighter than this are drawn red in the viewport. Only a warning: the curve is never changed." }).changed)
    {
      path.minTurnRadius = glm::clamp(path.minTurnRadius, MIN_TURN_RADIUS, MAX_TURN_RADIUS);
    }

    if (PropertyFloat("Steering Smoothing", path.curvatureSmoothing, {
      .min = 0.0f, .max = MAX_STEERING_SMOOTHING, .speed = 0.05f, .format = "%.1f", .unit = "m",
      .defaultValue = DEFAULT_STEERING_SMOOTHING,
      .tooltip = "Length along the path the curvature is averaged over, about one wheelbase. Keeps the front wheels "
                 "from snapping at every point." }).changed)
    {
      path.curvatureSmoothing = glm::clamp(path.curvatureSmoothing, 0.0f, MAX_STEERING_SMOOTHING);
      if (sessionActive)
        SequencerEditing::Repose(context);
    }
  }

  void ShotInspectorPanel::DrawPoints(EditorContext& context, MotionPathComponent& path)
  {
    PropertySubHeading("Points");

    Scene& scene = *context.scene;
    const Entity pathEntity = context.sequencerPath;
    SequencePlayer* player = context.sequencePlayer;
    const bool sessionActive = player != nullptr && player->IsActive();
    auto repose = [&context, sessionActive]() {
      if (sessionActive)
        SequencerEditing::Repose(context);
    };

    int32_t pointNumber = context.sequencerSelectedPoint + 1;
    if (PropertyInt("Selected Point", pointNumber, {
      .min = 0, .max = int32_t(path.points.size()), .speed = 0.05f,
      .tooltip = "Point edited below, counted from 1; 0 selects none. Points can also be clicked in the viewport, "
                 "where the gizmo moves the selected one." }).changed)
    {
      context.sequencerSelectedPoint = glm::clamp(pointNumber, 0, int32_t(path.points.size())) - 1;
    }

    const int selected = context.sequencerSelectedPoint;
    PointAction action = PointAction::None;
    if (selected >= 0 && selected < int(path.points.size()))
    {
      if (PropertyVec3("Position", path.points[selected], {
        .speed = 0.05f, .format = "%.2f", .unit = "m",
        .tooltip = "World-space position of the point" }).changed)
      {
        repose();
      }

      SuspendPropertyGrid();
      if (InlineButton("Insert After", {
        .icon = ICON_LC_PLUS,
        .tooltip = "Adds a point halfway to the next one, or further along past the end of the path" }))
      {
        action = PointAction::InsertAfter;
      }
      SameLineIfFits(ButtonWidth("Delete Point", ICON_LC_TRASH_2));
      if (InlineButton("Delete Point", { .icon = ICON_LC_TRASH_2 }))
        action = PointAction::Delete;
    }

    // Applied here, after the Position row: inserting or erasing reallocates
    if (action == PointAction::InsertAfter)
    {
      glm::vec3 point = path.points[selected];
      if (selected + 1 < int(path.points.size()))
      {
        point = 0.5f * (path.points[selected] + path.points[selected + 1]);
      }
      else
      {
        glm::vec3 direction = selected > 0 ? path.points[selected] - path.points[selected - 1] : glm::vec3(0.0f, 0.0f, 1.0f);
        direction.y = 0.0f;
        float length = glm::length(direction);
        point += (length > 1e-4f ? direction / length : glm::vec3(0.0f, 0.0f, 1.0f)) * APPEND_SPACING;
      }
      path.points.insert(path.points.begin() + selected + 1, point);
      context.sequencerSelectedPoint = selected + 1;
      repose();
    }
    else if (action == PointAction::Delete)
    {
      path.points.erase(path.points.begin() + selected);
      context.sequencerSelectedPoint = std::min(selected, int(path.points.size()) - 1);
      repose();
    }

    SuspendPropertyGrid();
    if (InlineButton("Add Point At Entity", {
      .icon = ICON_LC_MAP_PIN_PLUS,
      .tooltip = "Appends a point where the bound entity stands now. Drive the car there with the arrow keys first.",
      .disabledReason = sessionActive ? "A timeline session is posing the entity: Stop first" : nullptr }))
    {
      if (scene.HasComponent<WorldTransform>(pathEntity))
        AddPoint(context, path, glm::vec3(scene.GetComponent<WorldTransform>(pathEntity).world[3]));
    }

    SameLineIfFits(ButtonWidth("Add Point At View", ICON_LC_CROSSHAIR));
    if (InlineButton("Add Point At View", {
      .icon = ICON_LC_CROSSHAIR,
      .tooltip = "Appends a point where the centre of the editor view meets the ground, at the height of the last point" }))
    {
      Entity camera = entt::null;
      for (auto e : scene.GetView<EditorOnlyTag, CameraComponent>())
      {
        camera = e;
        break;
      }
      if (camera != entt::null)
      {
        const LocalTransform& view = scene.GetTransform(camera);
        glm::vec3 forward = glm::normalize(view.rotation * glm::vec3(0.0f, 0.0f, -1.0f));
        float groundY = !path.points.empty() ? path.points.back().y
          : scene.HasComponent<WorldTransform>(pathEntity) ? scene.GetComponent<WorldTransform>(pathEntity).world[3].y
          : 0.0f;

        float distance = std::abs(forward.y) > 1e-4f ? (groundY - view.position.y) / forward.y : -1.0f;
        if (distance > 0.0f && distance < MAX_VIEW_POINT_DISTANCE)
          AddPoint(context, path, view.position + forward * distance);
        else
          YA_LOG_WARN("Scene", "Shot Inspector: the view does not meet the ground at y = %.2f within %.0f m",
            groundY, MAX_VIEW_POINT_DISTANCE);
      }
    }
  }

  void ShotInspectorPanel::AddPoint(EditorContext& context, MotionPathComponent& path, const glm::vec3& position)
  {
    path.points.push_back(position);
    context.sequencerSelectedPoint = int(path.points.size()) - 1;

    if (context.sequencePlayer != nullptr && context.sequencePlayer->IsActive())
      SequencerEditing::Repose(context);
  }
}
