#include "Editor/Panels/SequencerPanel.h"

#include <imgui.h>

#include "Editor/EditorContext.h"
#include "Editor/SequencerEditing.h"
#include "Editor/Utils/EditorFonts.h"
#include "Editor/Utils/EditorIcons.h"
#include "Editor/Utils/EditorStyle.h"
#include "Editor/Utils/EditorWidgets.h"
#include "Scene/SequencePlayer.h"

namespace YAEngine
{
  using namespace EditorWidgets;

  namespace
  {
    constexpr float RULER_HEIGHT = 22.0f;
    // Tall enough to read the speed and turn curves drawn behind the keys
    constexpr float CAMERA_LANE_HEIGHT = 64.0f;
    // Tall enough to read the speed curve drawn across it
    constexpr float SPEED_LANE_HEIGHT = 56.0f;
    constexpr float SPEED_CURVE_MARGIN = 8.0f;
    constexpr float SPEED_CURVE_STEP_PX = 3.0f;
    constexpr float SHOT_ROW_HEIGHT = 16.0f;
    constexpr float SHOT_LANE_PADDING = 2.0f;
    constexpr int32_t MAX_SHOT_ROWS = 4;
    constexpr float MIN_CLIP_WIDTH = 6.0f;
    constexpr float CLIP_TEXT_INSET = 4.0f;
    // Below the lane label, so the curves never run through it
    constexpr float MOTION_CURVE_TOP = 18.0f;
    constexpr float MOTION_CURVE_BOTTOM = 6.0f;
    constexpr float MOTION_STEP_PX = 3.0f;
    constexpr int32_t MAX_MOTION_SAMPLES = 256;
    // Keeps the warning thresholds inside the lane when nothing exceeds them
    constexpr float MOTION_SCALE_HEADROOM = 1.15f;
    constexpr float MOTION_WARNING_FILL_ALPHA = 0.18f;
    constexpr float LANE_LABEL_INSET = 4.0f;
    constexpr float KEY_HALF_SIZE = 6.0f;
    constexpr float KEY_GRAB_PX = 9.0f;
    constexpr float KEY_RANGE_FILL_ALPHA = 0.15f;
    constexpr float KEY_RANGE_EDGE_ALPHA = 0.6f;
    constexpr float KEY_IN_RANGE_ALPHA = 0.6f;
    // Hit zone and drawn width of the stretch grips on the key range band's edges
    constexpr float KEY_RANGE_GRIP_PX = 6.0f;
    constexpr float KEY_RANGE_HANDLE_PX = 3.0f;
    // Time maps inside this margin at both ends, so a key diamond or tick label at either end of the view
    // stays inside the canvas
    constexpr float TIMELINE_PADDING_X = 10.0f;
    // Labels are ~40px wide, so this is what keeps them from colliding at any zoom
    constexpr float MIN_LABEL_SPACING_PX = 64.0f;
    constexpr float MIN_VIEW_SPAN = 0.25f;
    constexpr float MAX_VIEW_SPAN = 3600.0f;
    constexpr float WHEEL_ZOOM_FACTOR = 0.85f;
    // Share of the visible span one wheel notch pans
    constexpr float WHEEL_PAN_FRACTION = 0.1f;
    // A press on a selected key is a click until the mouse moves this far, so selecting never retimes a key
    constexpr float KEY_DRAG_DEAD_ZONE_PX = 6.0f;
    constexpr float SNAP_STEP = 0.05f;
    constexpr float PLAYHEAD_SNAP_PX = 6.0f;
    // Room left past the end when the view is fitted or widened to the timeline
    constexpr float VIEW_END_MARGIN = 1.1f;
    constexpr float MIN_FITTED_SPAN = 5.0f;

    // Next 1/2/5 * 10^n above the spacing the canvas can actually fit a label into
    float NiceTickStep(float span, float widthPx)
    {
      float rough = std::max(span, 1e-4f) * MIN_LABEL_SPACING_PX / std::max(widthPx, 1.0f);
      float magnitude = std::pow(10.0f, std::floor(std::log10(std::max(rough, 1e-4f))));
      float normalized = rough / magnitude;
      float factor = normalized <= 1.0f ? 1.0f : (normalized <= 2.0f ? 2.0f : (normalized <= 5.0f ? 5.0f : 10.0f));
      return factor * magnitude;
    }

    // Width of an InlineButton showing both the icon and the label
    float ButtonWidth(const char* label, const char* icon)
    {
      const ImGuiStyle& style = ImGui::GetStyle();
      return ImGui::CalcTextSize(icon).x + style.ItemInnerSpacing.x + ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f;
    }

    // Keeps the next item on the current line while it fits, so a narrow panel wraps the row
    // instead of clipping it
    void SameLineIfFits(float width)
    {
      ImGui::SameLine();
      if (ImGui::GetContentRegionAvail().x < width)
        ImGui::NewLine();
    }
  }

  void SequencerPanel::OnSceneReady(EditorContext& context)
  {
    m_FramedTrack = entt::null;
    m_FramedPath = entt::null;
    m_Drag = DragKind::None;
    m_DragKey = -1;
    b_DragMoved = false;
    m_DragTrack = entt::null;
    m_DragStartTimes.clear();
    m_DragPath = entt::null;
    m_DragStartSpeeds.clear();
    m_ShotClips.clear();
    m_MotionHash = 0;
    m_MotionSamples.clear();
    SequencerEditing::ResetForScene(context);
  }

  int32_t SequencerPanel::CollectShotClips(Scene& scene)
  {
    m_ShotClips.clear();
    for (auto [entity, track] : scene.GetView<CameraTrackComponent>().each())
    {
      if (!track.keys.empty())
        m_ShotClips.push_back({ .entity = entity, .start = SequencePlayer::ShotStart(track), .end = SequencePlayer::ShotEnd(track) });
    }
    // Storage order changes as components come and go; start order keeps the rows steady
    std::sort(m_ShotClips.begin(), m_ShotClips.end(), [](const ShotClip& a, const ShotClip& b) {
      return a.start != b.start ? a.start < b.start : entt::to_entity(a.entity) < entt::to_entity(b.entity);
    });

    std::array<float, MAX_SHOT_ROWS> rowEnds {};
    int32_t rows = 0;
    for (ShotClip& clip : m_ShotClips)
    {
      int32_t row = 0;
      while (row < rows && rowEnds[size_t(row)] > clip.start)
        row++;
      // Past the last row clips share it and overlap
      if (row >= MAX_SHOT_ROWS)
        row = MAX_SHOT_ROWS - 1;
      rows = std::max(rows, row + 1);
      rowEnds[size_t(row)] = std::max(rowEnds[size_t(row)], clip.end);
      clip.row = row;
    }
    return rows;
  }

  void SequencerPanel::UpdateMotionSamples(EditorContext& context, float start, float end, int32_t count)
  {
    uint64_t hash = SequencerEditing::HashTrackMotionInputs(*context.scene, context.sequencerTrack);
    auto mix = [&hash](uint64_t value) { hash ^= value + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2); };
    mix(std::hash<float>{}(start));
    mix(std::hash<float>{}(end));
    mix(uint64_t(count));
    if (hash == m_MotionHash && !m_MotionSamples.empty())
      return;

    m_MotionHash = hash;
    SequencerEditing::SampleTrackMotion(*context.scene, context.sequencerTrack, start, end, count, m_MotionSamples);
    m_MotionMaxSpeed = 0.0f;
    m_MotionMaxTurn = 0.0f;
    for (const SequencerEditing::MotionSample& sample : m_MotionSamples)
    {
      m_MotionMaxSpeed = std::max(m_MotionMaxSpeed, sample.speed);
      m_MotionMaxTurn = std::max(m_MotionMaxTurn, sample.turnRate);
    }
  }

  void SequencerPanel::EndDrag(EditorContext& context)
  {
    m_Drag = DragKind::None;
    m_DragKey = -1;
    m_DragStartTimes.clear();
    m_DragStartSpeeds.clear();
    context.sequencerDragActive = false;
  }

  void SequencerPanel::OnRender(EditorContext& context)
  {
    if (!BeginPanel())
    {
      EndDrag(context);
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
    DrawBindingRow(context);

    CameraTrackComponent* track = SequencerEditing::GetBoundTrack(context);
    MotionPathComponent* path = SequencerEditing::GetBoundPath(context);
    if (track == nullptr && path == nullptr)
    {
      EndDrag(context);
      SequencerEditing::DrawAddShotButton(context);
      ImGui::End();
      return;
    }

    if (m_FramedTrack != context.sequencerTrack || m_FramedPath != context.sequencerPath)
    {
      m_FramedTrack = context.sequencerTrack;
      m_FramedPath = context.sequencerPath;
      m_ViewStart = 0.0f;
      m_ViewEnd = std::max(SequencerEditing::GetTimelineEnd(context) * VIEW_END_MARGIN, MIN_FITTED_SPAN);
    }
    if (context.sequencerRevealTime >= 0.0f)
    {
      if (context.sequencerRevealTime > m_ViewEnd)
        m_ViewEnd = context.sequencerRevealTime * VIEW_END_MARGIN;
      context.sequencerRevealTime = -1.0f;
    }

    SequencerEditing::HandleHotkeys(context);
    DrawTransportRow(context, track, path);
    DrawStatus(context);
    DrawTimeline(context, track, path);
    context.sequencerDragActive = m_Drag != DragKind::None;

    ImGui::End();
  }

  void SequencerPanel::DrawBindingRow(EditorContext& context)
  {
    Scene& scene = *context.scene;
    entt::registry& registry = scene.GetRegistry();

    const bool hasTracks = !scene.GetView<CameraTrackComponent>().empty();
    const bool hasPaths = !scene.GetView<MotionPathComponent>().empty();
    const Entity boundTrack = context.sequencerTrack;
    const Entity boundPath = context.sequencerPath;

    BeginPropertyScope();
    Entity pickedTrack = boundTrack;
    const PropertyEdit trackEdit = PropertyEntity("Camera Track", pickedTrack, scene, {
      .allowNone = true,
      .filter = [&registry](Entity candidate) { return registry.all_of<CameraTrackComponent>(candidate); },
      .tooltip = "Camera whose track is the shot being authored. None plays the whole timeline through the active "
                 "camera. Selecting a camera that has a track binds it as well.",
      .disabledReason = hasTracks ? nullptr : "No camera in the scene has a Camera Track" });

    Entity pickedPath = boundPath;
    const PropertyEdit pathEdit = PropertyEntity("Motion Path", pickedPath, scene, {
      .allowNone = true,
      .filter = [&registry](Entity candidate) { return registry.all_of<MotionPathComponent>(candidate); },
      .tooltip = "Entity whose timed drive is shown on the Speed lane and in the viewport. Every motion path plays "
                 "on the same timeline whichever one is bound here.",
      .disabledReason = hasPaths ? nullptr : "No entity in the scene has a Motion Path" });
    EndPropertyScope();

    if (trackEdit.changed && pickedTrack != boundTrack)
    {
      // A selected track entity would bind itself again on the next frame
      if (pickedTrack == entt::null && context.selectedEntity == boundTrack)
        context.ClearSelection();
      SequencerEditing::BindTrack(context, pickedTrack);
      if (pickedTrack != entt::null)
        context.SelectEntity(pickedTrack);
    }

    if (pathEdit.changed && pickedPath != boundPath)
    {
      if (pickedPath == entt::null && context.selectedEntity == boundPath)
        context.ClearSelection();
      SequencerEditing::BindPath(context, pickedPath);
      if (pickedPath != entt::null)
        context.SelectEntity(pickedPath);
    }

    Entity selected = context.selectedEntity;
    const bool selectable = selected != entt::null && registry.valid(selected)
      && !scene.HasComponent<EditorOnlyTag>(selected);
    const bool canCreateTrack = selectable
      && scene.HasComponent<CameraComponent>(selected)
      && !scene.HasComponent<CameraTrackComponent>(selected)
      && !scene.HasComponent<MotionPathComponent>(selected);
    const bool canCreatePath = selectable
      && !scene.HasComponent<MotionPathComponent>(selected)
      && !scene.HasComponent<CameraTrackComponent>(selected);

    if (canCreateTrack && InlineButton("Create Camera Track", {
      .icon = ICON_LC_CIRCLE_PLUS,
      .tooltip = "Adds a Camera Track to the selected camera and binds it" }))
    {
      scene.AddComponent<CameraTrackComponent>(selected);
      SequencerEditing::BindTrack(context, selected);
    }

    if (canCreatePath)
    {
      if (canCreateTrack)
        SameLineIfFits(ButtonWidth("Create Motion Path", ICON_LC_SPLINE));
      if (InlineButton("Create Motion Path", {
        .icon = ICON_LC_SPLINE,
        .tooltip = "Adds a Motion Path to the selected entity and binds it. For a car the curve traces the rear axle." }))
      {
        scene.AddComponent<MotionPathComponent>(selected);
        SequencerEditing::BindPath(context, selected);
      }
    }

    if (context.sequencerTrack == entt::null && context.sequencerPath == entt::null && !canCreateTrack && !canCreatePath)
      ImGui::TextDisabled("Select a camera to author a shot, or any entity to give it a motion path.");
  }

  void SequencerPanel::DrawTransportRow(EditorContext& context, CameraTrackComponent* track, MotionPathComponent* path)
  {
    Scene& scene = *context.scene;
    SequencePlayer* player = context.sequencePlayer;

    const bool active = player != nullptr && player->IsActive();
    const bool advancing = player != nullptr && player->IsAdvancing();

    if (advancing)
    {
      if (InlineButton("Pause", { .icon = ICON_LC_PAUSE, .tooltip = "Holds the timeline where it is. Shortcut: Space" }))
        SequencerEditing::TogglePlayback(context);
    }
    else
    {
      if (InlineButton("Play", {
        .icon = ICON_LC_PLAY,
        .tooltip = "Plays the camera track's shot, or the whole timeline when no track is bound. After a scrub it "
                   "starts from the playhead; Stop and Play to start over. Shortcut: Space",
        .disabledReason = SequencerEditing::GetPlayUnavailableReason(context) }))
      {
        SequencerEditing::TogglePlayback(context);
      }
    }

    SameLineIfFits(ButtonWidth("Stop", ICON_LC_SQUARE));
    if (InlineButton("Stop", {
      .icon = ICON_LC_SQUARE,
      .tooltip = "Ends the session and puts back everything it moved",
      .disabledReason = active ? nullptr : "Nothing is playing or being scrubbed" }))
    {
      player->Stop(scene);
    }

    if (track != nullptr)
    {
      const bool piloting = context.pilotTrack == context.sequencerTrack;
      if (piloting)
      {
        SameLineIfFits(ButtonWidth("Exit Pilot", ICON_LC_X));
        if (InlineButton("Exit Pilot", {
          .icon = ICON_LC_X,
          .tooltip = "Puts the editor camera back where it was before the pilot. Shortcut: P, or Esc with the Sequencer "
                     "or the Shot Inspector focused (Esc over the viewport is left to the game)" }))
        {
          SequencerEditing::StopPilot(context);
        }
      }
      else
      {
        SameLineIfFits(ButtonWidth("Pilot", ICON_LC_DRONE));
        if (InlineButton("Pilot", {
          .icon = ICON_LC_DRONE,
          .tooltip = "Flies the editor camera from the track pose at the playhead, with the track fov and the 16:9 "
                     "output framed in the viewport. Moving the playhead puts it back on the track, K writes the "
                     "flown view as the key at the playhead, and playback makes it follow the shot. Shortcut: P",
          .disabledReason = SequencerEditing::GetPilotUnavailableReason(context, context.sequencerTrack) }))
        {
          SequencerEditing::StartPilot(context, context.sequencerTrack);
        }
      }

      const char* keyLabel = piloting ? "Set Key" : "Add Key";
      SameLineIfFits(ButtonWidth(keyLabel, ICON_LC_CIRCLE_PLUS));
      if (InlineButton(keyLabel, {
        .icon = ICON_LC_CIRCLE_PLUS,
        .tooltip = piloting
          ? "Writes the flown view, with the track fov, as the key at the playhead. A key already there keeps its "
            "time and roll. A new key moves with the Follow Target when the shot has one. Shortcut: K"
          : "Captures the editor camera pose at the playhead, with the track camera fov. A key already there is "
            "selected instead. A new key moves with the Follow Target when the shot has one. Shortcut: K" }))
      {
        SequencerEditing::SetKey(context);
      }
    }

    if (path != nullptr)
    {
      SameLineIfFits(ButtonWidth("Add Speed Key", ICON_LC_GAUGE));
      if (InlineButton("Add Speed Key", {
        .icon = ICON_LC_GAUGE,
        .tooltip = "Adds a speed key at the playhead, holding the speed the drive already has there. A key already "
                   "there is selected instead." }))
      {
        SequencerEditing::AddSpeedKey(context);
      }
    }

    SameLineIfFits(ButtonWidth("Add Shot", ICON_LC_FILM));
    SequencerEditing::DrawAddShotButton(context);

    char status[64];
    std::snprintf(status, sizeof(status), "%.2f / %.2f s", context.sequencerPlayhead, SequencerEditing::GetTimelineEnd(context));
    EditorFonts::Push(EditorFontRole::Mono);
    SameLineIfFits(ImGui::CalcTextSize(status).x);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(status);
    EditorFonts::Pop();
  }

  void SequencerPanel::DrawStatus(EditorContext& context)
  {
    if (context.sequencerStatus.empty())
      return;

    if (ImGui::GetTime() < context.sequencerStatusExpiry)
      PropertyStatus(nullptr, context.sequencerStatus.c_str(), StatusKind::Warning);
    else
      context.sequencerStatus.clear();
  }

  void SequencerPanel::DrawTimeline(EditorContext& context, CameraTrackComponent* track, MotionPathComponent* path)
  {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float width = std::max(ImGui::GetContentRegionAvail().x, 64.0f);
    const int32_t shotRows = CollectShotClips(*context.scene);
    float shotsLaneTop = origin.y + RULER_HEIGHT;
    float shotsLaneHeight = shotRows > 0 ? float(shotRows) * SHOT_ROW_HEIGHT + 2.0f * SHOT_LANE_PADDING : 0.0f;
    float cameraLaneTop = shotsLaneTop + shotsLaneHeight;
    float cameraLaneHeight = track != nullptr ? CAMERA_LANE_HEIGHT : 0.0f;
    // Camera keys sit halfway down the motion curves
    const float cameraKeyY = cameraLaneTop + (MOTION_CURVE_TOP + cameraLaneHeight - MOTION_CURVE_BOTTOM) * 0.5f;
    float speedLaneTop = cameraLaneTop + cameraLaneHeight;
    float speedLaneHeight = path != nullptr ? SPEED_LANE_HEIGHT : 0.0f;
    float height = RULER_HEIGHT + shotsLaneHeight + cameraLaneHeight + speedLaneHeight;
    ImVec2 canvasEnd(origin.x + width, origin.y + height);
    float timeStartX = origin.x + TIMELINE_PADDING_X;
    float timeWidth = width - 2.0f * TIMELINE_PADDING_X;

    ImGui::InvisibleButton("##sequencerTimeline", ImVec2(width, height),
      ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool activated = ImGui::IsItemActivated();
    auto isSpeedRangeDrag = [this]() {
      return m_Drag == DragKind::SpeedRangeMove || m_Drag == DragKind::SpeedRangeLeft || m_Drag == DragKind::SpeedRangeRight;
    };
    auto isRangeDrag = [this, &isSpeedRangeDrag]() {
      return m_Drag == DragKind::KeyRangeMove || m_Drag == DragKind::KeyRangeLeft || m_Drag == DragKind::KeyRangeRight
        || isSpeedRangeDrag();
    };
    const bool keyDrag = m_Drag == DragKind::CameraKey || m_Drag == DragKind::SpeedKey || isRangeDrag();

    ImGuiIO& io = ImGui::GetIO();
    // The plain wheel is left to the window so the panel scrolls over the timeline too. Claimed
    // wheels stop ImGui scrolling the window from the next frame on.
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelX);
    if (io.KeyCtrl || io.KeyShift)
      ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    // ImGui navigation would otherwise take Esc first and end the drag without restoring the key
    if (active && keyDrag)
      ImGui::SetItemKeyOwner(ImGuiKey_Escape);

    const float scale = EditorStyle::GetContentScale();

    // Members are read live, so a pan applied this frame maps correctly right away
    auto timeToX = [this, timeStartX, timeWidth](float t) {
      float span = std::max(m_ViewEnd - m_ViewStart, 1e-4f);
      return timeStartX + (t - m_ViewStart) / span * timeWidth;
    };
    auto xToTime = [this, timeStartX, timeWidth](float x) {
      float span = std::max(m_ViewEnd - m_ViewStart, 1e-4f);
      return m_ViewStart + (x - timeStartX) / timeWidth * span;
    };

    float maxSpeed = 1.0f;
    if (path != nullptr)
    {
      for (const MotionSpeedKey& key : path->speedKeys)
        maxSpeed = std::max(maxSpeed, key.speed);
    }
    auto speedToY = [speedLaneTop, speedLaneHeight, maxSpeed](float speed) {
      float usable = speedLaneHeight - 2.0f * SPEED_CURVE_MARGIN;
      return speedLaneTop + speedLaneHeight - SPEED_CURVE_MARGIN - speed / maxSpeed * usable;
    };

    if (hovered)
    {
      if (io.KeyCtrl && io.MouseWheel != 0.0f)
      {
        float span = std::max(m_ViewEnd - m_ViewStart, 1e-4f);
        float anchor = xToTime(ImGui::GetMousePos().x);
        float newSpan = glm::clamp(span * std::pow(WHEEL_ZOOM_FACTOR, io.MouseWheel), MIN_VIEW_SPAN, MAX_VIEW_SPAN);
        float ratio = (anchor - m_ViewStart) / span;
        m_ViewStart = anchor - ratio * newSpan;
        m_ViewEnd = m_ViewStart + newSpan;
      }
      else
      {
        // Shift + wheel arrives as the vertical wheel on Windows (ImGui swaps axes only inside its
        // own scrolling) but as the horizontal one where the OS converts it; preferring the
        // horizontal value pans once per notch either way. Positive pans toward earlier times.
        float notches = io.MouseWheelH != 0.0f ? io.MouseWheelH : (io.KeyShift ? io.MouseWheel : 0.0f);
        if (notches != 0.0f)
        {
          float shift = -notches * WHEEL_PAN_FRACTION * (m_ViewEnd - m_ViewStart);
          m_ViewStart += shift;
          m_ViewEnd += shift;
        }
      }
    }

    auto clipRect = [&](const ShotClip& clip, ImVec2& min, ImVec2& max) {
      const float x0 = timeToX(clip.start);
      min = ImVec2(x0, shotsLaneTop + SHOT_LANE_PADDING + float(clip.row) * SHOT_ROW_HEIGHT + 1.0f);
      max = ImVec2(std::max(timeToX(clip.end), x0 + MIN_CLIP_WIDTH), min.y + SHOT_ROW_HEIGHT - 2.0f);
    };
    // Topmost clip under the point, -1 for none
    auto hitClip = [&](ImVec2 point) -> int32_t {
      if (point.y < shotsLaneTop || point.y >= shotsLaneTop + shotsLaneHeight)
        return -1;
      for (int32_t i = int32_t(m_ShotClips.size()) - 1; i >= 0; i--)
      {
        ImVec2 min;
        ImVec2 max;
        clipRect(m_ShotClips[size_t(i)], min, max);
        if (point.x >= min.x && point.x < max.x && point.y >= min.y && point.y < max.y)
          return i;
      }
      return -1;
    };

    // The key nearest in time within grab range, on the lane under the point
    auto hitKey = [&](ImVec2 point) -> std::pair<DragKind, int> {
      auto nearest = [&timeToX, point](const auto& keys) {
        int hit = -1;
        float best = KEY_GRAB_PX;
        for (size_t i = 0; i < keys.size(); i++)
        {
          float distance = std::abs(timeToX(keys[i].time) - point.x);
          if (distance <= best)
          {
            best = distance;
            hit = int(i);
          }
        }
        return hit;
      };

      if (track != nullptr && point.y >= cameraLaneTop && point.y < cameraLaneTop + cameraLaneHeight)
      {
        int hit = nearest(track->keys);
        return { hit >= 0 ? DragKind::CameraKey : DragKind::None, hit };
      }
      if (path != nullptr && point.y >= speedLaneTop && point.y < speedLaneTop + speedLaneHeight)
      {
        int hit = nearest(path->speedKeys);
        return { hit >= 0 ? DragKind::SpeedKey : DragKind::None, hit };
      }
      return { DragKind::None, -1 };
    };

    enum class RangeHit : uint8_t { None, Inside, LeftEdge, RightEdge };
    struct KeyRangeHit
    {
      RangeHit kind = RangeHit::None;
      // The Speed lane's range rather than the Camera lane's
      bool speed = false;
      int first = -1;
      int last = -1;
      // Key of the range under the point; -1 on a grip or on empty band space
      int key = -1;
    };

    // The part of a lane's selected key range under the point: a stretch grip on either edge (away from the edge
    // key's diamond, which stays a key), a key of the range, or empty band space
    auto hitKeyRange = [&](ImVec2 point, bool speedLane) {
      KeyRangeHit result;
      result.speed = speedLane;
      const float laneTop = speedLane ? speedLaneTop : cameraLaneTop;
      const float laneHeight = speedLane ? speedLaneHeight : cameraLaneHeight;
      int first = -1;
      int last = -1;
      const bool selected = speedLane
        ? path != nullptr && SequencerEditing::GetBoundPath(context) == path
          && SequencerEditing::GetSpeedKeyRange(context, first, last)
        : track != nullptr && SequencerEditing::GetBoundTrack(context) == track
          && SequencerEditing::GetKeyRange(context, first, last);
      if (!selected || point.y < laneTop || point.y >= laneTop + laneHeight)
        return result;
      result.first = first;
      result.last = last;

      auto keyTime = [&](int index) {
        return speedLane ? path->speedKeys[size_t(index)].time : track->keys[size_t(index)].time;
      };
      auto keyY = [&](int index) {
        return speedLane ? speedToY(path->speedKeys[size_t(index)].speed) : cameraKeyY;
      };
      const float left = timeToX(keyTime(first));
      const float right = timeToX(keyTime(last));
      const float toLeft = std::abs(point.x - left);
      const float toRight = std::abs(point.x - right);
      const bool nearLeft = toLeft <= toRight;
      const bool onDiamond = std::abs(point.y - keyY(nearLeft ? first : last)) <= KEY_HALF_SIZE;
      if (!onDiamond && std::min(toLeft, toRight) <= 0.5f * KEY_RANGE_GRIP_PX * scale)
      {
        result.kind = nearLeft ? RangeHit::LeftEdge : RangeHit::RightEdge;
        return result;
      }

      const auto [lane, hit] = hitKey(point);
      if (lane == (speedLane ? DragKind::SpeedKey : DragKind::CameraKey))
      {
        if (hit >= first && hit <= last)
        {
          result.kind = RangeHit::Inside;
          result.key = hit;
        }
      }
      else if (point.x > left && point.x < right)
      {
        result.kind = RangeHit::Inside;
      }
      return result;
    };
    auto hitAnyKeyRange = [&](ImVec2 point) {
      KeyRangeHit camera = hitKeyRange(point, false);
      return camera.kind != RangeHit::None ? camera : hitKeyRange(point, true);
    };

    if (activated)
    {
      ImVec2 mouse = ImGui::GetMousePos();
      m_DragAnchorTime = xToTime(mouse.x);
      m_DragKey = -1;
      b_DragMoved = false;

      const int32_t clip = hitClip(mouse);
      const auto [shiftLane, shiftHit] = io.KeyShift ? hitKey(mouse) : std::pair<DragKind, int>(DragKind::None, -1);
      if (shiftLane == DragKind::CameraKey)
      {
        // Extends the key range; like any selecting press it never moves a key
        m_Drag = DragKind::None;
        SequencerEditing::ExtendKeyRange(context, shiftHit);
        SequencerEditing::SetPlayhead(context, track->keys[shiftHit].time);
      }
      else if (shiftLane == DragKind::SpeedKey)
      {
        m_Drag = DragKind::None;
        SequencerEditing::ExtendSpeedKeyRange(context, shiftHit);
        SequencerEditing::SetPlayhead(context, path->speedKeys[shiftHit].time);
      }
      else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || io.KeyShift)
      {
        m_Drag = DragKind::Pan;
      }
      else if (clip >= 0)
      {
        // Binds without scrubbing and keeps the zoom: the playhead stays where shots are being compared
        m_Drag = DragKind::None;
        const Entity clipEntity = m_ShotClips[size_t(clip)].entity;
        if (clipEntity != context.sequencerTrack)
        {
          // A selected entity with another track would take the binding back on the next frame
          Entity selected = context.selectedEntity;
          if (selected != clipEntity && context.scene->GetRegistry().valid(selected)
            && context.scene->HasComponent<CameraTrackComponent>(selected))
          {
            context.SelectEntity(clipEntity);
          }
          SequencerEditing::BindTrack(context, clipEntity);
          m_FramedTrack = clipEntity;
        }
      }
      else if (const KeyRangeHit range = hitAnyKeyRange(mouse); range.kind != RangeHit::None)
      {
        // The range stays selected: only a press that turns out to be a click selects a single key
        if (range.speed)
        {
          m_Drag = range.kind == RangeHit::LeftEdge ? DragKind::SpeedRangeLeft
            : range.kind == RangeHit::RightEdge ? DragKind::SpeedRangeRight
            : DragKind::SpeedRangeMove;
        }
        else
        {
          m_Drag = range.kind == RangeHit::LeftEdge ? DragKind::KeyRangeLeft
            : range.kind == RangeHit::RightEdge ? DragKind::KeyRangeRight
            : DragKind::KeyRangeMove;
        }
        m_DragKey = range.key;
        m_DragTrack = context.sequencerTrack;
        m_DragPath = context.sequencerPath;
        m_DragRangeFirst = range.first;
        m_DragRangeLast = range.last;
        m_DragStartPlayhead = context.sequencerPlayhead;
        m_DragStartTimes.clear();
        m_DragStartSpeeds.clear();
        if (range.speed)
        {
          for (const MotionSpeedKey& key : path->speedKeys)
          {
            m_DragStartTimes.push_back(key.time);
            m_DragStartSpeeds.push_back(key.speed);
          }
        }
        else
        {
          for (const CameraTrackKey& key : track->keys)
            m_DragStartTimes.push_back(key.time);
        }
      }
      else
      {
        auto [lane, hit] = hitKey(mouse);
        if (lane == DragKind::None)
        {
          m_Drag = DragKind::Scrub;
        }
        else
        {
          const bool camera = lane == DragKind::CameraKey;
          const int selected = camera ? context.sequencerSelectedKey : context.sequencerSelectedSpeedKey;
          float keyTime = camera ? track->keys[hit].time : path->speedKeys[hit].time;
          // A plain press leaves a single key selected
          if (camera)
            SequencerEditing::SelectKey(context, hit);
          else
            SequencerEditing::SelectSpeedKey(context, hit);

          if (selected == hit)
          {
            m_Drag = lane;
            m_DragKey = hit;
            m_DragStartTime = keyTime;
            m_DragStartPlayhead = context.sequencerPlayhead;
          }
          else
          {
            // Only a key selected before the press can move, so the rest of this press does nothing
            m_Drag = DragKind::None;
            SequencerEditing::SetPlayhead(context, keyTime);
          }
        }
      }
    }

    // Ctrl: onto the playhead the drag started at when it is close on screen, else onto the snap step
    auto snapTime = [&](float time) {
      if (!io.KeyCtrl)
        return time;
      if (std::abs(timeToX(time) - timeToX(m_DragStartPlayhead)) <= PLAYHEAD_SNAP_PX * scale)
        return m_DragStartPlayhead;
      return std::round(time / SNAP_STEP) * SNAP_STEP;
    };

    // Generic over the camera and the speed keys
    auto dragKey = [&](auto& keys) {
      if (m_DragKey < 0 || m_DragKey >= int(keys.size()))
      {
        m_Drag = DragKind::None;
        m_DragKey = -1;
        return;
      }

      auto& key = keys[m_DragKey];
      if (!active)
      {
        // Released inside the dead zone: a click on the selected key
        if (!b_DragMoved)
          SequencerEditing::SetPlayhead(context, key.time);
        m_Drag = DragKind::None;
        m_DragKey = -1;
        return;
      }

      if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      {
        if (b_DragMoved)
        {
          key.time = m_DragStartTime;
          SequencerEditing::SetPlayhead(context, m_DragStartPlayhead);
        }
        m_Drag = DragKind::None;
        m_DragKey = -1;
        return;
      }

      if (!b_DragMoved && !ImGui::IsMouseDragging(ImGuiMouseButton_Left, KEY_DRAG_DEAD_ZONE_PX * scale))
        return;
      b_DragMoved = true;

      const float time = snapTime(m_DragStartTime + xToTime(ImGui::GetMousePos().x) - m_DragAnchorTime);
      key.time = glm::clamp(time, SequencerEditing::KeyLowerBound(keys, m_DragKey), SequencerEditing::KeyUpperBound(keys, m_DragKey));
      SequencerEditing::SetPlayhead(context, key.time);

      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      ImGui::SetTooltip("%.3f s (%+.3f)", key.time, key.time - m_DragStartTime);
    };

    // The playhead follows the edge that moves: the range start for a move, else the dragged edge. A speed key range
    // also rescales its speeds so the car drives the same road.
    auto dragKeyRange = [&]() {
      const bool speed = isSpeedRangeDrag();
      auto endRangeDrag = [this]() {
        m_Drag = DragKind::None;
        m_DragKey = -1;
        m_DragStartTimes.clear();
        m_DragStartSpeeds.clear();
      };

      const size_t count = speed ? (path != nullptr ? path->speedKeys.size() : 0)
        : (track != nullptr ? track->keys.size() : 0);
      const bool sameKeys = speed
        ? path != nullptr && context.sequencerPath == m_DragPath && m_DragStartSpeeds.size() == count
        : track != nullptr && context.sequencerTrack == m_DragTrack;
      if (!sameKeys || m_DragStartTimes.size() != count || m_DragRangeFirst < 0 || m_DragRangeLast <= m_DragRangeFirst
        || size_t(m_DragRangeLast) >= count)
      {
        endRangeDrag();
        return;
      }

      auto keyTime = [&](size_t index) -> float& {
        return speed ? path->speedKeys[index].time : track->keys[index].time;
      };
      auto restoreKeys = [&]() {
        for (size_t i = 0; i < count; i++)
        {
          keyTime(i) = m_DragStartTimes[i];
          if (speed)
            path->speedKeys[i].speed = m_DragStartSpeeds[i];
        }
      };

      if (!active)
      {
        // Released inside the dead zone: a click, which selects the key it landed on alone or else scrubs
        if (!b_DragMoved && m_DragKey >= 0 && size_t(m_DragKey) < count)
        {
          if (speed)
            SequencerEditing::SelectSpeedKey(context, m_DragKey);
          else
            SequencerEditing::SelectKey(context, m_DragKey);
          SequencerEditing::SetPlayhead(context, keyTime(size_t(m_DragKey)));
        }
        else if (!b_DragMoved)
        {
          SequencerEditing::SetPlayhead(context, m_DragAnchorTime);
        }
        endRangeDrag();
        return;
      }

      if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      {
        if (b_DragMoved)
        {
          restoreKeys();
          SequencerEditing::SetPlayhead(context, m_DragStartPlayhead);
        }
        endRangeDrag();
        return;
      }

      if (!b_DragMoved && !ImGui::IsMouseDragging(ImGuiMouseButton_Left, KEY_DRAG_DEAD_ZONE_PX * scale))
        return;
      b_DragMoved = true;

      const int first = m_DragRangeFirst;
      const int last = m_DragRangeLast;
      const float start = m_DragStartTimes[size_t(first)];
      const float end = m_DragStartTimes[size_t(last)];
      const float offset = xToTime(ImGui::GetMousePos().x) - m_DragAnchorTime;
      restoreKeys();

      if (m_Drag == DragKind::KeyRangeMove || m_Drag == DragKind::SpeedRangeMove)
      {
        const float delta = snapTime(start + offset) - start;
        SpeedKeyTiming::Result result;
        if (speed)
          result = SequencerEditing::MoveSpeedKeyRange(path->speedKeys, first, last, delta);
        else
          result.applied = SequencerEditing::MoveKeyRange(track->keys, first, last, delta);
        SequencerEditing::SetPlayhead(context, keyTime(size_t(first)));
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        if (speed)
          ImGui::SetTooltip("start %.2f s (%+.2f), speeds x%.2f", keyTime(size_t(first)), result.applied, result.speedScale);
        else
          ImGui::SetTooltip("start %.2f s (%+.2f)", keyTime(size_t(first)), result.applied);
        return;
      }

      const bool right = m_Drag == DragKind::KeyRangeRight || m_Drag == DragKind::SpeedRangeRight;
      const float duration = right ? snapTime(end + offset) - start : end - snapTime(start + offset);
      const KeyRangeTiming::StretchAnchor anchor = right ? KeyRangeTiming::StretchAnchor::Start
        : KeyRangeTiming::StretchAnchor::End;
      SpeedKeyTiming::Result result;
      if (speed)
        result = SequencerEditing::StretchSpeedKeyRange(path->speedKeys, first, last, duration, anchor);
      else
        result.applied = SequencerEditing::StretchKeyRange(track->keys, first, last, duration, anchor);
      SequencerEditing::SetPlayhead(context, keyTime(size_t(right ? last : first)));
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      // Below 100% the range takes longer, so it plays slower
      const float pace = result.applied > 0.0f ? (end - start) / result.applied * 100.0f : 100.0f;
      if (speed)
      {
        ImGui::SetTooltip("%.2f-%.2f s: %.2f s -> %.2f s, pace %.0f%%, speeds x%.2f\nThe car drives the same road",
          start, end, end - start, result.applied, pace, result.speedScale);
      }
      else
      {
        ImGui::SetTooltip("%.2f-%.2f s: %.2f s -> %.2f s, pace %.0f%%", start, end, end - start, result.applied, pace);
      }
    };

    if (m_Drag == DragKind::CameraKey)
    {
      if (track != nullptr)
        dragKey(track->keys);
      else
        m_Drag = DragKind::None;
    }
    else if (m_Drag == DragKind::SpeedKey)
    {
      if (path != nullptr)
        dragKey(path->speedKeys);
      else
        m_Drag = DragKind::None;
    }
    else if (isRangeDrag())
    {
      dragKeyRange();
    }
    else if (m_Drag != DragKind::None && !active)
    {
      m_Drag = DragKind::None;
    }
    else if (m_Drag == DragKind::Pan)
    {
      float span = m_ViewEnd - m_ViewStart;
      m_ViewStart = m_DragAnchorTime - (ImGui::GetMousePos().x - timeStartX) / timeWidth * span;
      m_ViewEnd = m_ViewStart + span;
    }
    else if (m_Drag == DragKind::Scrub)
    {
      SequencerEditing::SetPlayhead(context, xToTime(ImGui::GetMousePos().x));
    }

    // A clip click above may have bound another track; the rest of this frame still draws the old one
    const bool motionShown = track != nullptr && track->keys.size() >= 2
      && SequencerEditing::GetBoundTrack(context) == track;
    int rangeFirst = -1;
    int rangeLast = -1;
    const bool rangeShown = track != nullptr && SequencerEditing::GetBoundTrack(context) == track
      && SequencerEditing::GetKeyRange(context, rangeFirst, rangeLast);
    if (motionShown)
    {
      const float rangeStart = std::max(m_ViewStart, SequencePlayer::ShotStart(*track));
      const float rangeEnd = std::min(m_ViewEnd, SequencePlayer::ShotEnd(*track));
      if (rangeEnd > rangeStart)
      {
        const int32_t count = glm::clamp(int32_t((timeToX(rangeEnd) - timeToX(rangeStart)) / MOTION_STEP_PX) + 1,
          2, MAX_MOTION_SAMPLES);
        UpdateMotionSamples(context, rangeStart, rangeEnd, count);
      }
      else
      {
        m_MotionSamples.clear();
        m_MotionHash = 0;
      }
    }

    RangeHit hoveredRange = RangeHit::None;
    bool hoveredSpeedRange = false;
    if (hovered && !active)
    {
      const ImVec2 mouse = ImGui::GetMousePos();
      auto [lane, hit] = hitKey(mouse);
      const int32_t clip = hitClip(mouse);
      // With Shift a press extends the range or pans, so the range takes none
      if (!io.KeyShift)
      {
        const KeyRangeHit rangeHit = hitAnyKeyRange(mouse);
        hoveredRange = rangeHit.kind;
        hoveredSpeedRange = rangeHit.speed;
      }
      const bool onGrip = hoveredRange == RangeHit::LeftEdge || hoveredRange == RangeHit::RightEdge;
      const bool inCameraLane = mouse.y >= cameraLaneTop && mouse.y < cameraLaneTop + cameraLaneHeight;
      const bool inSpeedLane = mouse.y >= speedLaneTop && mouse.y < speedLaneTop + speedLaneHeight;
      if (onGrip)
      {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      }
      else if (lane != DragKind::None)
      {
        const int selected = lane == DragKind::CameraKey ? context.sequencerSelectedKey : context.sequencerSelectedSpeedKey;
        const bool extends = io.KeyShift;
        ImGui::SetMouseCursor(hoveredRange == RangeHit::Inside ? ImGuiMouseCursor_ResizeAll
          : hit == selected && !extends ? ImGuiMouseCursor_ResizeEW
          : ImGuiMouseCursor_Hand);
      }
      else if (clip >= 0)
      {
        const ShotClip& shot = m_ShotClips[size_t(clip)];
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        char unnamed[32];
        ImGui::SetTooltip("%s\n%.2f - %.2f s%s",
          EditorCommands::GetEntityDisplayName(context.scene->GetRegistry(), shot.entity, unnamed), shot.start, shot.end,
          shot.entity == context.sequencerTrack ? "" : "\nClick to edit this shot");
      }
      else if ((inCameraLane || inSpeedLane) && hoveredRange == RangeHit::Inside)
      {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
      }

      if (!onGrip && lane == DragKind::None && clip < 0 && inCameraLane && motionShown && !m_MotionSamples.empty())
      {
        const float time = xToTime(mouse.x);
        const SequencerEditing::MotionSample* nearest = nullptr;
        for (const SequencerEditing::MotionSample& sample : m_MotionSamples)
        {
          if (nearest == nullptr || std::abs(sample.time - time) < std::abs(nearest->time - time))
            nearest = &sample;
        }
        if (nearest != nullptr && std::abs(timeToX(nearest->time) - mouse.x) <= MOTION_STEP_PX * 2.0f)
        {
          ImGui::SetTooltip("%.2f s\nCamera speed %.1f m/s\nCamera turn %.0f deg/s", nearest->time, nearest->speed,
            nearest->turnRate);
        }
      }
    }

    const EditorTheme& theme = EditorStyle::GetTheme();
    const auto themeColor = [](const glm::vec4& color) { return ImGui::GetColorU32(ToImGuiColor(color)); };

    draw->PushClipRect(origin, canvasEnd, true);

    draw->AddRectFilled(origin, canvasEnd, themeColor(theme.appBackground));
    draw->AddRectFilled(origin, ImVec2(canvasEnd.x, origin.y + RULER_HEIGHT), themeColor(theme.panel));

    float step = NiceTickStep(m_ViewEnd - m_ViewStart, timeWidth);
    // Panning past the start would otherwise label negative seconds
    float first = std::max(std::ceil(m_ViewStart / step) * step, 0.0f);
    int tickCount = int((m_ViewEnd - first) / step) + 1;
    for (int i = 0; i < tickCount && i < 256; i++)
    {
      float t = first + step * float(i);
      float x = timeToX(t);
      draw->AddLine(ImVec2(x, origin.y), ImVec2(x, canvasEnd.y), themeColor(theme.borderSubtle));

      char label[24];
      std::snprintf(label, sizeof(label), step < 1.0f ? "%.2f" : "%.1f", t);
      // A label the right edge would cut is left out; its tick still shows
      if (x + 3.0f + ImGui::CalcTextSize(label).x <= canvasEnd.x)
        draw->AddText(ImVec2(x + 3.0f, origin.y + 3.0f), themeColor(theme.textSecondary), label);
    }

    auto drawKey = [&](float x, float y, const glm::vec4& color) {
      ImVec2 points[4] = {
        ImVec2(x, y - KEY_HALF_SIZE), ImVec2(x + KEY_HALF_SIZE, y),
        ImVec2(x, y + KEY_HALF_SIZE), ImVec2(x - KEY_HALF_SIZE, y)
      };
      draw->AddConvexPolyFilled(points, 4, themeColor(color));
      draw->AddPolyline(points, 4, themeColor(theme.appBackground), ImDrawFlags_Closed, 1.5f);
    };

    if (shotsLaneHeight > 0.0f)
    {
      draw->AddLine(ImVec2(origin.x, shotsLaneTop), ImVec2(canvasEnd.x, shotsLaneTop), themeColor(theme.borderSubtle));
      for (const ShotClip& clip : m_ShotClips)
      {
        ImVec2 min;
        ImVec2 max;
        clipRect(clip, min, max);
        const bool bound = clip.entity == context.sequencerTrack;
        draw->AddRectFilled(min, max, themeColor(bound ? theme.accent : theme.accentMuted), 2.0f);
        draw->AddRect(min, max, themeColor(bound ? theme.textPrimary : theme.borderStrong), 2.0f);

        const ImVec2 textMin(std::max(min.x, origin.x) + CLIP_TEXT_INSET, min.y);
        if (max.x - textMin.x > CLIP_TEXT_INSET)
        {
          const float textY = min.y + (max.y - min.y - ImGui::GetTextLineHeight()) * 0.5f;
          char unnamed[32];
          draw->PushClipRect(textMin, ImVec2(max.x - 1.0f, max.y), true);
          draw->AddText(ImVec2(textMin.x, textY), themeColor(bound ? theme.appBackground : theme.textPrimary),
            EditorCommands::GetEntityDisplayName(context.scene->GetRegistry(), clip.entity, unnamed));
          draw->PopClipRect();
        }
      }
    }

    glm::vec4 inRangeColor = theme.warning;
    inRangeColor.a *= KEY_IN_RANGE_ALPHA;
    glm::vec4 rangeFill = theme.warning;
    rangeFill.a *= KEY_RANGE_FILL_ALPHA;
    glm::vec4 rangeEdge = theme.warning;
    rangeEdge.a *= KEY_RANGE_EDGE_ALPHA;

    if (track != nullptr)
    {
      draw->AddLine(ImVec2(origin.x, cameraLaneTop), ImVec2(canvasEnd.x, cameraLaneTop), themeColor(theme.borderSubtle));
      if (!track->keys.empty())
      {
        glm::vec4 shotColor = theme.accentMuted;
        shotColor.a *= 0.5f;
        draw->AddRectFilled(ImVec2(timeToX(SequencePlayer::ShotStart(*track)), cameraLaneTop + 4.0f),
          ImVec2(timeToX(SequencePlayer::ShotEnd(*track)), cameraLaneTop + cameraLaneHeight - 4.0f),
          themeColor(shotColor));
      }

      if (rangeShown)
      {
        const ImVec2 rangeMin(timeToX(track->keys[size_t(rangeFirst)].time), cameraLaneTop + 2.0f);
        const ImVec2 rangeMax(timeToX(track->keys[size_t(rangeLast)].time), cameraLaneTop + cameraLaneHeight - 2.0f);
        draw->AddRectFilled(rangeMin, rangeMax, themeColor(rangeFill));
        draw->AddRect(rangeMin, rangeMax, themeColor(rangeEdge));
      }

      if (motionShown && m_MotionSamples.size() >= 2)
      {
        const float curveTop = cameraLaneTop + MOTION_CURVE_TOP;
        const float curveBottom = cameraLaneTop + cameraLaneHeight - MOTION_CURVE_BOTTOM;
        const float speedScale = std::max(m_MotionMaxSpeed, SequencerEditing::MOTION_SPEED_WARNING) * MOTION_SCALE_HEADROOM;
        const float turnScale = std::max(m_MotionMaxTurn, SequencerEditing::MOTION_TURN_WARNING) * MOTION_SCALE_HEADROOM;
        auto valueToY = [curveTop, curveBottom](float value, float scale) {
          return curveBottom - glm::clamp(value / scale, 0.0f, 1.0f) * (curveBottom - curveTop);
        };
        auto exceeds = [](const SequencerEditing::MotionSample& sample) {
          return sample.speed > SequencerEditing::MOTION_SPEED_WARNING
            || sample.turnRate > SequencerEditing::MOTION_TURN_WARNING;
        };

        // Stretches a viewer will find too fast get a red band behind them
        glm::vec4 warningFill = theme.error;
        warningFill.a *= MOTION_WARNING_FILL_ALPHA;
        const float halfStep = 0.5f * (timeToX(m_MotionSamples[1].time) - timeToX(m_MotionSamples[0].time));
        for (const SequencerEditing::MotionSample& sample : m_MotionSamples)
        {
          if (!exceeds(sample))
            continue;
          const float x = timeToX(sample.time);
          draw->AddRectFilled(ImVec2(x - halfStep, cameraLaneTop + 1.0f),
            ImVec2(x + halfStep, cameraLaneTop + cameraLaneHeight - 1.0f), themeColor(warningFill));
        }

        glm::vec4 speedLimitColor = theme.accent;
        speedLimitColor.a *= 0.35f;
        glm::vec4 turnLimitColor = theme.info;
        turnLimitColor.a *= 0.35f;
        const float speedLimitY = valueToY(SequencerEditing::MOTION_SPEED_WARNING, speedScale);
        const float turnLimitY = valueToY(SequencerEditing::MOTION_TURN_WARNING, turnScale);
        draw->AddLine(ImVec2(origin.x, speedLimitY), ImVec2(canvasEnd.x, speedLimitY), themeColor(speedLimitColor));
        if (std::abs(turnLimitY - speedLimitY) > 1.0f)
          draw->AddLine(ImVec2(origin.x, turnLimitY), ImVec2(canvasEnd.x, turnLimitY), themeColor(turnLimitColor));

        for (size_t i = 1; i < m_MotionSamples.size(); i++)
        {
          const SequencerEditing::MotionSample& a = m_MotionSamples[i - 1];
          const SequencerEditing::MotionSample& b = m_MotionSamples[i];
          const float xa = timeToX(a.time);
          const float xb = timeToX(b.time);
          const bool fast = a.speed > SequencerEditing::MOTION_SPEED_WARNING
            || b.speed > SequencerEditing::MOTION_SPEED_WARNING;
          const bool turning = a.turnRate > SequencerEditing::MOTION_TURN_WARNING
            || b.turnRate > SequencerEditing::MOTION_TURN_WARNING;
          draw->AddLine(ImVec2(xa, valueToY(a.turnRate, turnScale)), ImVec2(xb, valueToY(b.turnRate, turnScale)),
            themeColor(turning ? theme.error : theme.info), turning ? 2.0f : 1.5f);
          draw->AddLine(ImVec2(xa, valueToY(a.speed, speedScale)), ImVec2(xb, valueToY(b.speed, speedScale)),
            themeColor(fast ? theme.error : theme.accent), fast ? 2.0f : 1.5f);
        }
      }

      float labelX = origin.x + LANE_LABEL_INSET;
      const float labelY = cameraLaneTop + 2.0f;
      draw->AddText(ImVec2(labelX, labelY), themeColor(theme.textDisabled), "Camera");
      if (motionShown && !m_MotionSamples.empty())
      {
        char speedLabel[48];
        char turnLabel[48];
        std::snprintf(speedLabel, sizeof(speedLabel), "speed up to %.1f m/s", m_MotionMaxSpeed);
        std::snprintf(turnLabel, sizeof(turnLabel), "turn up to %.0f deg/s", m_MotionMaxTurn);
        const float gap = ImGui::CalcTextSize("  ").x;
        labelX += ImGui::CalcTextSize("Camera").x + gap;
        draw->AddText(ImVec2(labelX, labelY), themeColor(m_MotionMaxSpeed > SequencerEditing::MOTION_SPEED_WARNING
          ? theme.error : theme.accent), speedLabel);
        labelX += ImGui::CalcTextSize(speedLabel).x + gap;
        draw->AddText(ImVec2(labelX, labelY), themeColor(m_MotionMaxTurn > SequencerEditing::MOTION_TURN_WARNING
          ? theme.error : theme.info), turnLabel);
      }

      if (rangeShown)
      {
        // Over the curves and under the diamonds, which stay keys where they cover a grip
        glm::vec4 gripColor = theme.warning;
        gripColor.a *= KEY_RANGE_EDGE_ALPHA;
        const float halfWidth = 0.5f * KEY_RANGE_HANDLE_PX * scale;
        auto drawGrip = [&](float time, bool hot) {
          const float x = timeToX(time);
          draw->AddRectFilled(ImVec2(x - halfWidth, cameraLaneTop + MOTION_CURVE_TOP),
            ImVec2(x + halfWidth, cameraLaneTop + cameraLaneHeight - MOTION_CURVE_BOTTOM),
            themeColor(hot ? theme.warning : gripColor), halfWidth);
        };
        drawGrip(track->keys[size_t(rangeFirst)].time,
          m_Drag == DragKind::KeyRangeLeft || (!hoveredSpeedRange && hoveredRange == RangeHit::LeftEdge));
        drawGrip(track->keys[size_t(rangeLast)].time,
          m_Drag == DragKind::KeyRangeRight || (!hoveredSpeedRange && hoveredRange == RangeHit::RightEdge));
      }

      for (size_t i = 0; i < track->keys.size(); i++)
      {
        const int index = int(i);
        const bool inRange = rangeShown && index >= rangeFirst && index <= rangeLast;
        const glm::vec4& color = index == context.sequencerSelectedKey ? theme.warning
          : inRange ? inRangeColor
          : theme.accent;
        drawKey(timeToX(track->keys[i].time), cameraKeyY, color);
      }
    }

    if (path != nullptr)
    {
      draw->AddLine(ImVec2(origin.x, speedLaneTop), ImVec2(canvasEnd.x, speedLaneTop), themeColor(theme.borderSubtle));

      const MotionPathTable& table = path->GetTable();
      if (!path->speedKeys.empty() && path->points.size() >= 2)
      {
        float driveEnd = MotionPathDuration(table, path->speedKeys);
        draw->AddRectFilled(ImVec2(timeToX(path->speedKeys.front().time), speedLaneTop + 4.0f),
          ImVec2(timeToX(driveEnd), speedLaneTop + speedLaneHeight - 4.0f), themeColor(theme.accentMuted));
      }

      int speedFirst = -1;
      int speedLast = -1;
      const bool speedRangeShown = SequencerEditing::GetBoundPath(context) == path
        && SequencerEditing::GetSpeedKeyRange(context, speedFirst, speedLast);
      if (speedRangeShown)
      {
        const ImVec2 rangeMin(timeToX(path->speedKeys[size_t(speedFirst)].time), speedLaneTop + 2.0f);
        const ImVec2 rangeMax(timeToX(path->speedKeys[size_t(speedLast)].time), speedLaneTop + speedLaneHeight - 2.0f);
        draw->AddRectFilled(rangeMin, rangeMax, themeColor(rangeFill));
        draw->AddRect(rangeMin, rangeMax, themeColor(rangeEdge));
      }

      char label[48];
      std::snprintf(label, sizeof(label), "Speed  %.0f m/s", maxSpeed);
      draw->AddText(ImVec2(origin.x + LANE_LABEL_INSET, speedLaneTop + 2.0f), themeColor(theme.textDisabled), label);

      if (!path->speedKeys.empty())
      {
        // The speed the drive really has, so the stop at the end of the path shows too
        std::vector<ImVec2> curve;
        for (float x = timeStartX; x <= timeStartX + timeWidth; x += SPEED_CURVE_STEP_PX)
        {
          float speed = EvaluateMotionPath(table, path->speedKeys, xToTime(x)).speed;
          curve.push_back(ImVec2(x, speedToY(speed)));
        }
        if (curve.size() >= 2)
          draw->AddPolyline(curve.data(), int(curve.size()), themeColor(theme.accent), ImDrawFlags_None, 1.5f);
      }

      if (speedRangeShown)
      {
        glm::vec4 gripColor = theme.warning;
        gripColor.a *= KEY_RANGE_EDGE_ALPHA;
        const float halfWidth = 0.5f * KEY_RANGE_HANDLE_PX * scale;
        auto drawGrip = [&](float time, bool hot) {
          const float x = timeToX(time);
          draw->AddRectFilled(ImVec2(x - halfWidth, speedLaneTop + SPEED_CURVE_MARGIN),
            ImVec2(x + halfWidth, speedLaneTop + speedLaneHeight - SPEED_CURVE_MARGIN),
            themeColor(hot ? theme.warning : gripColor), halfWidth);
        };
        drawGrip(path->speedKeys[size_t(speedFirst)].time,
          m_Drag == DragKind::SpeedRangeLeft || (hoveredSpeedRange && hoveredRange == RangeHit::LeftEdge));
        drawGrip(path->speedKeys[size_t(speedLast)].time,
          m_Drag == DragKind::SpeedRangeRight || (hoveredSpeedRange && hoveredRange == RangeHit::RightEdge));
      }

      for (size_t i = 0; i < path->speedKeys.size(); i++)
      {
        const MotionSpeedKey& key = path->speedKeys[i];
        const int index = int(i);
        const bool inRange = speedRangeShown && index >= speedFirst && index <= speedLast;
        drawKey(timeToX(key.time), speedToY(key.speed), index == context.sequencerSelectedSpeedKey ? theme.warning
          : inRange ? inRangeColor
          : theme.accent);
      }
    }

    if ((m_Drag == DragKind::CameraKey || m_Drag == DragKind::SpeedKey || isRangeDrag()) && b_DragMoved)
    {
      // The playhead follows the dragged key or range edge, so this marks where it was, which is what Ctrl
      // snaps to
      glm::vec4 snapColor = theme.error;
      snapColor.a *= 0.45f;
      float snapX = timeToX(m_DragStartPlayhead);
      draw->AddLine(ImVec2(snapX, origin.y), ImVec2(snapX, canvasEnd.y), themeColor(snapColor), 1.0f);
    }

    float playheadX = timeToX(context.sequencerPlayhead);
    draw->AddLine(ImVec2(playheadX, origin.y), ImVec2(playheadX, canvasEnd.y), themeColor(theme.error), 2.0f);

    draw->PopClipRect();
    draw->AddRect(origin, canvasEnd, themeColor(theme.borderSubtle));
  }
}
