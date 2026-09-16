#include "Editor/Panels/SequencerPanel.h"

#include <imgui.h>

#include "Editor/EditorCommands.h"
#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorFonts.h"
#include "Editor/Utils/EditorIcons.h"
#include "Editor/Utils/EditorStyle.h"
#include "Editor/Utils/EditorWidgets.h"
#include "Scene/SequencePlayer.h"
#include "Utils/CameraOrientation.h"

namespace YAEngine
{
  using namespace EditorWidgets;

  namespace
  {
    // Two keys at the same time make a zero-length segment the evaluators cannot use
    constexpr float MIN_KEY_GAP = 0.01f;
    constexpr float RULER_HEIGHT = 22.0f;
    constexpr float LANE_HEIGHT = 34.0f;
    // Tall enough to read the speed curve drawn across it
    constexpr float SPEED_LANE_HEIGHT = 56.0f;
    constexpr float SPEED_CURVE_MARGIN = 8.0f;
    constexpr float SPEED_CURVE_STEP_PX = 3.0f;
    constexpr float LANE_LABEL_INSET = 4.0f;
    constexpr float KEY_HALF_SIZE = 6.0f;
    constexpr float KEY_GRAB_PX = 9.0f;
    // Time maps inside this margin at both ends, so a key diamond or tick label at either end of the view
    // stays inside the canvas
    constexpr float TIMELINE_PADDING_X = 10.0f;
    // Labels are ~40px wide, so this is what keeps them from colliding at any zoom
    constexpr float MIN_LABEL_SPACING_PX = 64.0f;
    constexpr float MIN_VIEW_SPAN = 0.25f;
    constexpr float MAX_VIEW_SPAN = 3600.0f;
    constexpr float DUPLICATE_OFFSET = 0.5f;
    constexpr float MIN_FOV_DEGREES = 10.0f;
    constexpr float MAX_FOV_DEGREES = 120.0f;
    constexpr float ROTATION_EPSILON = 1e-6f;

    constexpr float MAX_SPEED = 100.0f;
    constexpr float DEFAULT_SPEED = 10.0f;
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

    constexpr EnumOption ROTATION_MODES[] = {
      { .label = "Keyframed", .tooltip = "The camera takes the rotation stored in the keys" },
      { .label = "Aim At Entity", .tooltip = "The camera turns toward the Aim Target; the keyed rotation is used while no "
                                             "entity has that name" },
    };

    bool HasTrack(Scene& scene, Entity e)
    {
      return e != entt::null && scene.GetRegistry().valid(e) && scene.HasComponent<CameraTrackComponent>(e);
    }

    bool HasPath(Scene& scene, Entity e)
    {
      return e != entt::null && scene.GetRegistry().valid(e) && scene.HasComponent<MotionPathComponent>(e);
    }

    // Next 1/2/5 * 10^n above the spacing the canvas can actually fit a label into
    float NiceTickStep(float span, float widthPx)
    {
      float rough = std::max(span, 1e-4f) * MIN_LABEL_SPACING_PX / std::max(widthPx, 1.0f);
      float magnitude = std::pow(10.0f, std::floor(std::log10(std::max(rough, 1e-4f))));
      float normalized = rough / magnitude;
      float factor = normalized <= 1.0f ? 1.0f : (normalized <= 2.0f ? 2.0f : (normalized <= 5.0f ? 5.0f : 10.0f));
      return factor * magnitude;
    }

    Entity FindEditorCamera(Scene& scene)
    {
      for (auto e : scene.GetView<EditorOnlyTag, CameraComponent>())
        return e;
      return entt::null;
    }

    // Keys are authored from the editor camera, never from the active one: while the track
    // camera is previewed it IS the active camera and its transform is being driven by the
    // track, so capturing it would just record the pose the track already produces. The fov
    // comes from the track camera instead - the editor camera's fov is not what is authored.
    bool CaptureViewPose(EditorContext& context, Entity trackEntity, CameraTrackKey& key)
    {
      Scene& scene = *context.scene;
      Entity source = FindEditorCamera(scene);
      if (source == entt::null)
        return false;

      // The editor camera is a root entity, so its LocalTransform is already world space
      const LocalTransform& transform = scene.GetTransform(source);
      key.position = transform.position;
      key.rotation = glm::normalize(transform.rotation);

      if (scene.HasComponent<CameraComponent>(trackEntity))
        key.fov = scene.GetComponent<CameraComponent>(trackEntity).fov;

      return true;
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

    // Bounds that keep a dragged key between its neighbours, so the sorted invariant holds
    // without resorting
    template<typename Key>
    float KeyLowerBound(const std::vector<Key>& keys, int index)
    {
      return index > 0 ? keys[index - 1].time + MIN_KEY_GAP : 0.0f;
    }

    template<typename Key>
    float KeyUpperBound(const std::vector<Key>& keys, int index)
    {
      float lower = KeyLowerBound(keys, index);
      if (index + 1 >= int(keys.size()))
        return std::max(lower, keys[index].time + MAX_VIEW_SPAN);
      return std::max(lower, keys[index + 1].time - MIN_KEY_GAP);
    }

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
    m_Track = entt::null;
    m_Path = entt::null;
    m_FramedTrack = entt::null;
    m_FramedPath = entt::null;
    m_SelectedKey = -1;
    m_SelectedSpeedKey = -1;
    m_SelectedPoint = -1;
    m_Playhead = 0.0f;
    m_Drag = DragKind::None;
    m_DragKey = -1;
    m_EulerTrack = entt::null;
    m_EulerKey = -1;
    context.sequencerTrack = entt::null;
    context.sequencerSelectedKey = -1;
    context.sequencerKeyPickRequest = -1;
    context.sequencerScrubRequest = -1.0f;
    context.sequencerPath = entt::null;
    context.sequencerSelectedPoint = -1;
    context.sequencerPointPickRequest = -1;
  }

  void SequencerPanel::OnRender(EditorContext& context)
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

    ResolveBinding(context);
    DrawBindingRow(context);

    Scene& scene = *context.scene;
    CameraTrackComponent* track = HasTrack(scene, m_Track) ? &scene.GetComponent<CameraTrackComponent>(m_Track) : nullptr;
    MotionPathComponent* path = HasPath(scene, m_Path) ? &scene.GetComponent<MotionPathComponent>(m_Path) : nullptr;

    if (track == nullptr && path == nullptr)
    {
      context.sequencerTrack = entt::null;
      context.sequencerSelectedKey = -1;
      context.sequencerPath = entt::null;
      context.sequencerSelectedPoint = -1;
      ImGui::End();
      return;
    }

    if (m_FramedTrack != m_Track || m_FramedPath != m_Path)
    {
      m_FramedTrack = m_Track;
      m_FramedPath = m_Path;
      m_SelectedKey = -1;
      m_SelectedSpeedKey = -1;
      m_SelectedPoint = -1;
      m_ViewStart = 0.0f;
      m_ViewEnd = std::max(TimelineEnd(context) * 1.1f, 5.0f);
    }

    if (track == nullptr || m_SelectedKey >= int(track->keys.size()))
      m_SelectedKey = -1;
    if (path == nullptr || m_SelectedSpeedKey >= int(path->speedKeys.size()))
      m_SelectedSpeedKey = -1;
    if (path == nullptr || m_SelectedPoint >= int(path->points.size()))
      m_SelectedPoint = -1;

    // Picks made in the viewport; adopted here because the panel republishes its own
    // selection into the context at the end of every render
    if (context.sequencerKeyPickRequest >= 0)
    {
      if (track != nullptr && !track->keys.empty())
        m_SelectedKey = std::min(context.sequencerKeyPickRequest, int(track->keys.size()) - 1);
      context.sequencerKeyPickRequest = -1;
    }
    if (context.sequencerPointPickRequest >= 0)
    {
      if (path != nullptr && !path->points.empty())
        m_SelectedPoint = std::min(context.sequencerPointPickRequest, int(path->points.size()) - 1);
      context.sequencerPointPickRequest = -1;
    }

    // A viewport key drag already posed the camera; this keeps the playhead in step
    if (context.sequencerScrubRequest >= 0.0f)
    {
      m_Playhead = context.sequencerScrubRequest;
      context.sequencerScrubRequest = -1.0f;
    }

    DrawTransportRow(context, track, path);
    DrawTimeline(context, track, path);
    if (track != nullptr)
      DrawKeyInspector(context, *track);
    if (path != nullptr)
    {
      DrawSpeedKeyInspector(context, *path);
      DrawPathSettings(context, *path);
    }
    if (track != nullptr)
      DrawSettings(context, *track);

    context.sequencerTrack = track != nullptr ? m_Track : Entity(entt::null);
    context.sequencerSelectedKey = m_SelectedKey;
    context.sequencerPath = path != nullptr ? m_Path : Entity(entt::null);
    context.sequencerSelectedPoint = m_SelectedPoint;

    ImGui::End();
  }

  void SequencerPanel::ResolveBinding(EditorContext& context)
  {
    Scene& scene = *context.scene;

    // Selecting a track or path entity binds it, but the binding then sticks: selecting the
    // aim target or a light to check something must not empty the panel mid-session.
    if (HasTrack(scene, context.selectedEntity))
      m_Track = context.selectedEntity;
    else if (!HasTrack(scene, m_Track))
      m_Track = entt::null;

    if (HasPath(scene, context.selectedEntity))
      m_Path = context.selectedEntity;
    else if (!HasPath(scene, m_Path))
      m_Path = entt::null;

    // The only motion path binds itself: the car belongs on every shot being authored
    if (m_Path == entt::null)
    {
      Entity only = entt::null;
      int count = 0;
      for (Entity candidate : scene.GetView<MotionPathComponent>())
      {
        only = candidate;
        count++;
      }
      if (count == 1)
        m_Path = only;
    }
  }

  void SequencerPanel::DrawBindingRow(EditorContext& context)
  {
    Scene& scene = *context.scene;
    entt::registry& registry = scene.GetRegistry();

    const bool hasTracks = !scene.GetView<CameraTrackComponent>().empty();
    const bool hasPaths = !scene.GetView<MotionPathComponent>().empty();

    BeginPropertyScope();
    Entity pickedTrack = m_Track;
    const PropertyEdit trackEdit = PropertyEntity("Camera Track", pickedTrack, scene, {
      .allowNone = true,
      .filter = [&registry](Entity candidate) { return registry.all_of<CameraTrackComponent>(candidate); },
      .tooltip = "Camera whose track is the shot being authored. None plays the whole timeline through the active "
                 "camera. Selecting a camera that has a track binds it as well.",
      .disabledReason = hasTracks ? nullptr : "No camera in the scene has a Camera Track" });

    Entity pickedPath = m_Path;
    const PropertyEdit pathEdit = PropertyEntity("Motion Path", pickedPath, scene, {
      .allowNone = true,
      .filter = [&registry](Entity candidate) { return registry.all_of<MotionPathComponent>(candidate); },
      .tooltip = "Entity whose timed drive is shown on the Speed lane and in the viewport. Every motion path plays "
                 "on the same timeline whichever one is bound here.",
      .disabledReason = hasPaths ? nullptr : "No entity in the scene has a Motion Path" });
    EndPropertyScope();

    if (trackEdit.changed && pickedTrack != m_Track)
    {
      // A selected track entity would bind itself again on the next frame
      if (pickedTrack == entt::null && context.selectedEntity == m_Track)
        context.ClearSelection();
      m_Track = pickedTrack;
      m_SelectedKey = -1;
      if (pickedTrack != entt::null)
        context.SelectEntity(pickedTrack);
    }

    if (pathEdit.changed && pickedPath != m_Path)
    {
      if (pickedPath == entt::null && context.selectedEntity == m_Path)
        context.ClearSelection();
      m_Path = pickedPath;
      m_SelectedSpeedKey = -1;
      m_SelectedPoint = -1;
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
      m_Track = selected;
      m_SelectedKey = -1;
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
        m_Path = selected;
        m_SelectedSpeedKey = -1;
        m_SelectedPoint = -1;
      }
    }

    if (m_Track == entt::null && m_Path == entt::null && !canCreateTrack && !canCreatePath)
      ImGui::TextDisabled("Select a camera to author a shot, or any entity to give it a motion path.");
  }

  void SequencerPanel::DrawTransportRow(EditorContext& context, CameraTrackComponent* track, MotionPathComponent* path)
  {
    Scene& scene = *context.scene;
    SequencePlayer* player = context.sequencePlayer;
    Entity playTrack = PlayableTrack(context);

    const bool active = player != nullptr && player->IsActive();
    const bool advancing = player != nullptr && player->IsAdvancing();
    // One timeline: whatever started the session, the playhead shows its time
    if (advancing)
      m_Playhead = float(player->GetTime());

    if (advancing)
    {
      if (InlineButton("Pause", { .icon = ICON_LC_PAUSE }))
        player->Pause();
    }
    else
    {
      const bool drivable = path != nullptr && path->points.size() >= 2;
      const char* playReason = player == nullptr ? "The editor has no sequence player"
        : playTrack != entt::null || drivable ? nullptr
        : track != nullptr ? "The camera track has no keys"
        : "The motion path needs at least two points";
      if (InlineButton("Play", {
        .icon = ICON_LC_PLAY,
        .tooltip = "Plays the camera track's shot, or the whole timeline when no track is bound. After a scrub it "
                   "starts from the playhead; Stop and Play to start over.",
        .disabledReason = playReason }))
      {
        if (!EditorCommands::PlaySequence(*player, scene, playTrack))
          m_Playhead = float(player->GetTime());
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
      // Playback already hands the viewport to the track camera, so the two must not both
      // fight over the active camera
      const bool shown = active && player->HoldsCamera() && player->GetCameraTrack() == m_Track;
      const char* previewReason = shown ? "Playback already shows the track camera" : nullptr;
      if (context.previewCamera == m_Track)
      {
        SameLineIfFits(ButtonWidth("Stop Preview", ICON_LC_X));
        if (InlineButton("Stop Preview", { .icon = ICON_LC_X, .disabledReason = previewReason }))
          context.StopCameraPreview();
      }
      else
      {
        SameLineIfFits(ButtonWidth("Preview", ICON_LC_VIDEO));
        if (InlineButton("Preview", {
          .icon = ICON_LC_VIDEO,
          .tooltip = "Looks through the track camera in the viewport",
          .disabledReason = previewReason }))
        {
          context.StartCameraPreview(m_Track);
        }
      }

      SameLineIfFits(ButtonWidth("Add Key", ICON_LC_CIRCLE_PLUS));
      if (InlineButton("Add Key", {
        .icon = ICON_LC_CIRCLE_PLUS,
        .tooltip = "Captures the editor camera pose at the playhead, with the track camera fov" }))
      {
        AddKeyFromView(context, *track);
      }
    }

    if (path != nullptr)
    {
      SameLineIfFits(ButtonWidth("Add Speed Key", ICON_LC_GAUGE));
      if (InlineButton("Add Speed Key", {
        .icon = ICON_LC_GAUGE,
        .tooltip = "Adds a speed key at the playhead, holding the speed the drive already has there" }))
      {
        AddSpeedKey(context, *path);
      }
    }

    char status[64];
    std::snprintf(status, sizeof(status), "%.2f / %.2f s", m_Playhead, TimelineEnd(context));
    EditorFonts::Push(EditorFontRole::Mono);
    SameLineIfFits(ImGui::CalcTextSize(status).x);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(status);
    EditorFonts::Pop();
  }

  void SequencerPanel::DrawTimeline(EditorContext& context, CameraTrackComponent* track, MotionPathComponent* path)
  {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float width = std::max(ImGui::GetContentRegionAvail().x, 64.0f);
    float cameraLaneTop = origin.y + RULER_HEIGHT;
    float cameraLaneHeight = track != nullptr ? LANE_HEIGHT : 0.0f;
    float speedLaneTop = cameraLaneTop + cameraLaneHeight;
    float speedLaneHeight = path != nullptr ? SPEED_LANE_HEIGHT : 0.0f;
    float height = RULER_HEIGHT + cameraLaneHeight + speedLaneHeight;
    ImVec2 canvasEnd(origin.x + width, origin.y + height);
    float timeStartX = origin.x + TIMELINE_PADDING_X;
    float timeWidth = width - 2.0f * TIMELINE_PADDING_X;

    ImGui::InvisibleButton("##sequencerTimeline", ImVec2(width, height),
      ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);

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

    if (ImGui::IsItemHovered())
    {
      float wheel = ImGui::GetIO().MouseWheel;
      if (wheel != 0.0f)
      {
        float span = std::max(m_ViewEnd - m_ViewStart, 1e-4f);
        float anchor = xToTime(ImGui::GetMousePos().x);
        float newSpan = glm::clamp(span * std::pow(0.85f, wheel), MIN_VIEW_SPAN, MAX_VIEW_SPAN);
        float ratio = (anchor - m_ViewStart) / span;
        m_ViewStart = anchor - ratio * newSpan;
        m_ViewEnd = m_ViewStart + newSpan;
      }
    }

    if (ImGui::IsItemActivated())
    {
      ImVec2 mouse = ImGui::GetMousePos();
      if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::GetIO().KeyCtrl)
      {
        m_Drag = DragKind::Pan;
        m_PanAnchorTime = xToTime(mouse.x);
      }
      else
      {
        // The key nearest in time within grab range, on the lane under the mouse
        auto nearestKey = [&timeToX, mouse](const auto& keys) {
          int hit = -1;
          float best = KEY_GRAB_PX;
          for (size_t i = 0; i < keys.size(); i++)
          {
            float distance = std::abs(timeToX(keys[i].time) - mouse.x);
            if (distance <= best)
            {
              best = distance;
              hit = int(i);
            }
          }
          return hit;
        };

        m_Drag = DragKind::Scrub;
        if (track != nullptr && mouse.y >= cameraLaneTop && mouse.y < cameraLaneTop + cameraLaneHeight)
        {
          int hit = nearestKey(track->keys);
          if (hit >= 0)
          {
            m_SelectedKey = hit;
            m_DragKey = hit;
            m_Drag = DragKind::CameraKey;
          }
        }
        else if (path != nullptr && mouse.y >= speedLaneTop && mouse.y < speedLaneTop + speedLaneHeight)
        {
          int hit = nearestKey(path->speedKeys);
          if (hit >= 0)
          {
            m_SelectedSpeedKey = hit;
            m_DragKey = hit;
            m_Drag = DragKind::SpeedKey;
          }
        }
      }
    }

    if (m_Drag != DragKind::None)
    {
      if (!ImGui::IsItemActive())
      {
        m_Drag = DragKind::None;
        m_DragKey = -1;
      }
      else
      {
        ImVec2 mouse = ImGui::GetMousePos();
        if (m_Drag == DragKind::Pan)
        {
          float span = m_ViewEnd - m_ViewStart;
          m_ViewStart = m_PanAnchorTime - (mouse.x - timeStartX) / timeWidth * span;
          m_ViewEnd = m_ViewStart + span;
        }
        else if (m_Drag == DragKind::CameraKey && track != nullptr
          && m_DragKey >= 0 && m_DragKey < int(track->keys.size()))
        {
          track->keys[m_DragKey].time = glm::clamp(xToTime(mouse.x),
            KeyLowerBound(track->keys, m_DragKey), KeyUpperBound(track->keys, m_DragKey));
          SetPlayhead(context, track->keys[m_DragKey].time);
        }
        else if (m_Drag == DragKind::SpeedKey && path != nullptr
          && m_DragKey >= 0 && m_DragKey < int(path->speedKeys.size()))
        {
          path->speedKeys[m_DragKey].time = glm::clamp(xToTime(mouse.x),
            KeyLowerBound(path->speedKeys, m_DragKey), KeyUpperBound(path->speedKeys, m_DragKey));
          SetPlayhead(context, path->speedKeys[m_DragKey].time);
        }
        else if (m_Drag == DragKind::Scrub)
        {
          SetPlayhead(context, xToTime(mouse.x));
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

    auto drawKey = [&](float x, float y, bool selected) {
      ImVec2 points[4] = {
        ImVec2(x, y - KEY_HALF_SIZE), ImVec2(x + KEY_HALF_SIZE, y),
        ImVec2(x, y + KEY_HALF_SIZE), ImVec2(x - KEY_HALF_SIZE, y)
      };
      draw->AddConvexPolyFilled(points, 4, themeColor(selected ? theme.warning : theme.accent));
      draw->AddPolyline(points, 4, themeColor(theme.appBackground), ImDrawFlags_Closed, 1.5f);
    };

    if (track != nullptr)
    {
      if (!track->keys.empty())
      {
        draw->AddRectFilled(ImVec2(timeToX(SequencePlayer::ShotStart(*track)), cameraLaneTop + 4.0f),
          ImVec2(timeToX(SequencePlayer::ShotEnd(*track)), cameraLaneTop + cameraLaneHeight - 4.0f),
          themeColor(theme.accentMuted));
      }
      draw->AddText(ImVec2(origin.x + LANE_LABEL_INSET, cameraLaneTop + 2.0f), themeColor(theme.textDisabled), "Camera");

      float laneY = cameraLaneTop + cameraLaneHeight * 0.5f;
      for (size_t i = 0; i < track->keys.size(); i++)
        drawKey(timeToX(track->keys[i].time), laneY, int(i) == m_SelectedKey);
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

      for (size_t i = 0; i < path->speedKeys.size(); i++)
      {
        const MotionSpeedKey& key = path->speedKeys[i];
        drawKey(timeToX(key.time), speedToY(key.speed), int(i) == m_SelectedSpeedKey);
      }
    }

    float playheadX = timeToX(m_Playhead);
    draw->AddLine(ImVec2(playheadX, origin.y), ImVec2(playheadX, canvasEnd.y), themeColor(theme.error), 2.0f);

    draw->PopClipRect();
    draw->AddRect(origin, canvasEnd, themeColor(theme.borderSubtle));
  }

  void SequencerPanel::DrawKeyInspector(EditorContext& context, CameraTrackComponent& track)
  {
    if (!BeginPropertyGroup("Camera Key", { .icon = ICON_LC_KEY, .tooltip = "The camera key selected in the timeline or in the viewport" }))
      return;

    if (m_SelectedKey < 0 || m_SelectedKey >= int(track.keys.size()))
    {
      PropertyStatus(nullptr, "Click a key on the Camera lane to edit it. Wheel zooms, Ctrl-drag pans.");
      EndPropertyGroup();
      return;
    }

    int index = m_SelectedKey;
    float lower = KeyLowerBound(track.keys, index);
    float upper = KeyUpperBound(track.keys, index);
    KeyAction action = KeyAction::None;

    {
      CameraTrackKey& key = track.keys[index];

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
        SetPlayhead(context, key.time);
      }

      float fovDegrees = glm::degrees(key.fov);
      if (PropertyFloat("FOV", fovDegrees, {
        .min = MIN_FOV_DEGREES, .max = MAX_FOV_DEGREES, .speed = 0.25f, .format = "%.1f", .unit = "deg",
        .tooltip = "Vertical field of view at this key, eased toward the neighbouring keys in between." }).changed)
      {
        key.fov = glm::radians(glm::clamp(fovDegrees, MIN_FOV_DEGREES, MAX_FOV_DEGREES));
        SetPlayhead(context, m_Playhead);
      }

      if (PropertyVec3("Position", key.position, {
        .speed = 0.05f, .format = "%.2f", .unit = "m",
        .tooltip = "World-space camera position at this key." }).changed)
      {
        SetPlayhead(context, m_Playhead);
      }

      const bool cached = m_EulerTrack == m_Track && m_EulerKey == index && SameRotation(m_EulerRotation, key.rotation);
      glm::vec3 angles = cached ? m_EulerDegrees : glm::degrees(YawPitchRollFromRotation(key.rotation));
      if (PropertyVec3("Rotation", angles, {
        .speed = 0.5f, .format = "%.1f", .unit = "deg", .componentLabels = { "Yaw", "Pitch", "Roll" },
        .tooltip = "World-space camera rotation at this key: yaw about world up, then pitch, then roll about the view "
                   "direction. With Rotation Mode Aim At Entity it is used only while the Aim Target matches no entity." }).changed)
      {
        key.rotation = MakeYawPitchRollRotation(glm::radians(angles));
        SetPlayhead(context, m_Playhead);
      }
      m_EulerTrack = m_Track;
      m_EulerKey = index;
      m_EulerRotation = key.rotation;
      m_EulerDegrees = angles;

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
    }

    // Applied after the reference above is out of scope: inserting or erasing reallocates
    switch (action)
    {
      case KeyAction::UpdateFromView:
      {
        CameraTrackKey updated = track.keys[index];
        if (CaptureViewPose(context, m_Track, updated))
        {
          track.keys[index] = updated;
          SetPlayhead(context, m_Playhead);
        }
        break;
      }
      case KeyAction::Duplicate:
      {
        CameraTrackKey copy = track.keys[index];
        float offset = DUPLICATE_OFFSET;
        if (index + 1 < int(track.keys.size()))
          offset = std::min(offset, (track.keys[index + 1].time - copy.time) * 0.5f);
        copy.time += std::max(offset, MIN_KEY_GAP);
        m_SelectedKey = InsertKey(track, copy);
        SetPlayhead(context, copy.time);
        break;
      }
      case KeyAction::Delete:
      {
        track.keys.erase(track.keys.begin() + index);
        m_SelectedKey = -1;
        SetPlayhead(context, m_Playhead);
        break;
      }
      case KeyAction::None:
        break;
    }

    EndPropertyGroup();
  }

  void SequencerPanel::DrawSettings(EditorContext& context, CameraTrackComponent& track)
  {
    if (!BeginPropertyGroup("Track Settings", { .icon = ICON_LC_SLIDERS_HORIZONTAL, .tooltip = "How the whole track plays" }))
      return;

    Scene& scene = *context.scene;
    entt::registry& registry = scene.GetRegistry();

    PropertyEnum("Rotation Mode", track.rotationMode, ROTATION_MODES, {
      .defaultValue = 0,
      .tooltip = "Where the camera's rotation comes from while the track plays" });

    const bool aiming = track.rotationMode == CameraTrackComponent::RotationMode::AimAt;
    // Stored as a name, resolved to an entity only for the picker, so the scene format stays as it was
    Entity target = m_AimTargetLookup.Resolve(scene, track.aimTargetName);
    const Entity trackEntity = m_Track;
    const PropertyEdit targetEdit = PropertyEntity("Aim Target", target, scene, {
      .allowNone = true,
      .uniqueNames = true,
      .filter = [trackEntity](Entity candidate) { return candidate != trackEntity; },
      .tooltip = "Entity the camera turns toward. Stored by its name, so renaming that entity breaks the link, and an "
                 "entity whose name another entity shares cannot be picked.",
      .disabledReason = aiming ? nullptr : "Requires Rotation Mode: Aim At Entity" });
    if (targetEdit.changed)
    {
      const Name* name = target != entt::null ? registry.try_get<Name>(target) : nullptr;
      track.aimTargetName = name != nullptr ? *name : std::string();
      target = m_AimTargetLookup.Resolve(scene, track.aimTargetName);
    }

    if (aiming && !track.aimTargetName.empty() && target == entt::null)
    {
      const std::string missing = "No entity is named '" + track.aimTargetName + "'";
      PropertyStatus(nullptr, missing.c_str(), StatusKind::Warning,
        "The camera falls back to the keyed rotation while the name matches no entity. Pick a target or None.");
    }
    else if (aiming && m_AimTargetLookup.GetMatchCount() > 1)
    {
      const std::string ambiguous = std::to_string(m_AimTargetLookup.GetMatchCount()) + " entities are named '"
        + track.aimTargetName + "'";
      PropertyStatus(nullptr, ambiguous.c_str(), StatusKind::Warning,
        "The camera aims at whichever of them the scene lists first, which can change as entities are added or "
        "removed. Rename all but one of them, or pick another target.");
    }

    PropertyBool("Reset PostFX On Start", track.resetPostFXOnStart, {
      .defaultValue = true,
      .tooltip = "Clears the temporal history and the auto exposure when playback starts, so the shot does not inherit "
                 "them from the previous viewpoint" });

    EndPropertyGroup();
  }

  void SequencerPanel::DrawSpeedKeyInspector(EditorContext& context, MotionPathComponent& path)
  {
    if (!BeginPropertyGroup("Speed Key", { .icon = ICON_LC_GAUGE, .tooltip = "The speed key selected on the Speed lane" }))
      return;

    if (m_SelectedSpeedKey < 0 || m_SelectedSpeedKey >= int(path.speedKeys.size()))
    {
      PropertyStatus(nullptr, "Click a key on the Speed lane to edit it. Add Speed Key places one at the playhead.");
      EndPropertyGroup();
      return;
    }

    int index = m_SelectedSpeedKey;
    float lower = KeyLowerBound(path.speedKeys, index);
    float upper = KeyUpperBound(path.speedKeys, index);
    bool erase = false;

    {
      MotionSpeedKey& key = path.speedKeys[index];

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
        SetPlayhead(context, key.time);
      }

      float speed = key.speed;
      if (PropertyFloat("Speed", speed, {
        .min = 0.0f, .max = MAX_SPEED, .speed = 0.05f, .format = "%.2f", .unit = "m/s",
        .tooltip = "Speed at this key, eased toward the neighbouring keys in between. After the last key it holds. A "
                   "drive never goes backwards." }).changed)
      {
        key.speed = glm::clamp(speed, 0.0f, MAX_SPEED);
        SetPlayhead(context, m_Playhead);
      }

      char kmh[32];
      std::snprintf(kmh, sizeof(kmh), "%.1f km/h", key.speed * MS_TO_KMH);
      PropertyReadOnly("In km/h", kmh, { .mono = true });

      char distance[32];
      std::snprintf(distance, sizeof(distance), "%.2f m", EvaluateMotionTiming(path.speedKeys, key.time).distance);
      PropertyReadOnly("Distance", distance, { .mono = true, .tooltip = "How far along the path the entity is at this key" });

      SuspendPropertyGrid();
      if (InlineButton("Delete", { .icon = ICON_LC_TRASH_2 }))
        erase = true;
    }

    if (erase)
    {
      path.speedKeys.erase(path.speedKeys.begin() + index);
      m_SelectedSpeedKey = -1;
      SetPlayhead(context, m_Playhead);
    }

    EndPropertyGroup();
  }

  void SequencerPanel::DrawPathSettings(EditorContext& context, MotionPathComponent& path)
  {
    if (!BeginPropertyGroup("Motion Path", { .icon = ICON_LC_SPLINE, .tooltip = "The curve the bound entity drives along" }))
      return;

    Scene& scene = *context.scene;
    SequencePlayer* player = context.sequencePlayer;
    const bool sessionActive = player != nullptr && player->IsActive();
    // Only a running session is re-posed by curve edits: a stopped one must not start just
    // because a point moved
    auto repose = [this, &context, sessionActive]() {
      if (sessionActive)
        SetPlayhead(context, m_Playhead);
    };

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
      repose();
    }

    PropertySubHeading("Points");

    int32_t pointNumber = m_SelectedPoint + 1;
    if (PropertyInt("Selected Point", pointNumber, {
      .min = 0, .max = int32_t(path.points.size()), .speed = 0.05f,
      .tooltip = "Point edited below, counted from 1; 0 selects none. Points can also be clicked in the viewport, "
                 "where the gizmo moves the selected one." }).changed)
    {
      m_SelectedPoint = glm::clamp(pointNumber, 0, int32_t(path.points.size())) - 1;
    }

    PointAction action = PointAction::None;
    if (m_SelectedPoint >= 0 && m_SelectedPoint < int(path.points.size()))
    {
      if (PropertyVec3("Position", path.points[m_SelectedPoint], {
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
    int selected = m_SelectedPoint;
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
      m_SelectedPoint = selected + 1;
      repose();
    }
    else if (action == PointAction::Delete)
    {
      path.points.erase(path.points.begin() + selected);
      m_SelectedPoint = std::min(selected, int(path.points.size()) - 1);
      repose();
    }

    SuspendPropertyGrid();
    if (InlineButton("Add Point At Entity", {
      .icon = ICON_LC_MAP_PIN_PLUS,
      .tooltip = "Appends a point where the bound entity stands now. Drive the car there with the arrow keys first.",
      .disabledReason = sessionActive ? "A timeline session is posing the entity: Stop first" : nullptr }))
    {
      if (scene.HasComponent<WorldTransform>(m_Path))
        AddPoint(context, path, glm::vec3(scene.GetComponent<WorldTransform>(m_Path).world[3]));
    }

    SameLineIfFits(ButtonWidth("Add Point At View", ICON_LC_CROSSHAIR));
    if (InlineButton("Add Point At View", {
      .icon = ICON_LC_CROSSHAIR,
      .tooltip = "Appends a point where the centre of the editor view meets the ground, at the height of the last point" }))
    {
      Entity camera = FindEditorCamera(scene);
      if (camera != entt::null)
      {
        const LocalTransform& view = scene.GetTransform(camera);
        glm::vec3 forward = glm::normalize(view.rotation * glm::vec3(0.0f, 0.0f, -1.0f));
        float groundY = !path.points.empty() ? path.points.back().y
          : scene.HasComponent<WorldTransform>(m_Path) ? scene.GetComponent<WorldTransform>(m_Path).world[3].y
          : 0.0f;

        float distance = std::abs(forward.y) > 1e-4f ? (groundY - view.position.y) / forward.y : -1.0f;
        if (distance > 0.0f && distance < MAX_VIEW_POINT_DISTANCE)
          AddPoint(context, path, view.position + forward * distance);
        else
          YA_LOG_WARN("Scene", "Sequencer: the view does not meet the ground at y = %.2f within %.0f m",
            groundY, MAX_VIEW_POINT_DISTANCE);
      }
    }

    EndPropertyGroup();
  }

  void SequencerPanel::AddKeyFromView(EditorContext& context, CameraTrackComponent& track)
  {
    CameraTrackKey key;
    if (!CaptureViewPose(context, m_Track, key))
    {
      YA_LOG_WARN("Scene", "Sequencer: no editor camera to capture a key from");
      return;
    }

    float time = m_Playhead;
    for (const auto& existing : track.keys)
    {
      // Landing on top of an existing key would make a zero-length segment, and the intent
      // right after adding one is to place the next further along anyway
      if (std::abs(existing.time - time) < MIN_KEY_GAP)
      {
        time = track.keys.back().time + 1.0f;
        break;
      }
    }

    key.time = time;
    m_SelectedKey = InsertKey(track, key);

    float end = TimelineEnd(context);
    if (end > m_ViewEnd)
      m_ViewEnd = end * 1.1f;

    SetPlayhead(context, key.time);
  }

  void SequencerPanel::AddSpeedKey(EditorContext& context, MotionPathComponent& path)
  {
    MotionSpeedKey key;
    key.time = m_Playhead;
    key.speed = path.speedKeys.empty() ? DEFAULT_SPEED : EvaluateMotionTiming(path.speedKeys, m_Playhead).speed;

    for (const auto& existing : path.speedKeys)
    {
      if (std::abs(existing.time - key.time) < MIN_KEY_GAP)
      {
        key.time = path.speedKeys.back().time + 1.0f;
        break;
      }
    }

    m_SelectedSpeedKey = InsertSpeedKey(path, key);

    float end = TimelineEnd(context);
    if (end > m_ViewEnd)
      m_ViewEnd = end * 1.1f;

    SetPlayhead(context, key.time);
  }

  void SequencerPanel::AddPoint(EditorContext& context, MotionPathComponent& path, const glm::vec3& position)
  {
    path.points.push_back(position);
    m_SelectedPoint = int(path.points.size()) - 1;

    if (context.sequencePlayer != nullptr && context.sequencePlayer->IsActive())
      SetPlayhead(context, m_Playhead);
  }

  void SequencerPanel::SetPlayhead(EditorContext& context, float time)
  {
    m_Playhead = glm::clamp(time, 0.0f, TimelineEnd(context));

    // Scrubbing poses the whole timeline. Without a session it opens a preview that leaves the
    // viewport where it is, so the frustum and path gizmos show the result from the editor camera.
    if (context.sequencePlayer != nullptr)
      context.sequencePlayer->Scrub(*context.scene, PlayableTrack(context), double(m_Playhead));
  }

  float SequencerPanel::TimelineEnd(EditorContext& context) const
  {
    return float(SequencePlayer::TimelineDuration(*context.scene));
  }

  Entity SequencerPanel::PlayableTrack(EditorContext& context) const
  {
    Scene& scene = *context.scene;
    if (!HasTrack(scene, m_Track) || !scene.HasComponent<CameraComponent>(m_Track))
      return entt::null;
    return scene.GetComponent<CameraTrackComponent>(m_Track).keys.empty() ? Entity(entt::null) : m_Track;
  }
}
