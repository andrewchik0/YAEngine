#include "Editor/Panels/SequencerPanel.h"

#include <imgui.h>

#include "Editor/EditorCommands.h"
#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorFonts.h"
#include "Editor/Utils/EditorIcons.h"
#include "Editor/Utils/EditorStyle.h"
#include "Editor/Utils/EditorWidgets.h"
#include "Scene/CameraTrackPlayer.h"
#include "Utils/CameraOrientation.h"

namespace YAEngine
{
  using namespace EditorWidgets;

  namespace
  {
    // Two keys at the same time make a zero-length segment the evaluator cannot use
    constexpr float MIN_KEY_GAP = 0.01f;
    constexpr float RULER_HEIGHT = 22.0f;
    constexpr float LANE_HEIGHT = 34.0f;
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

    constexpr EnumOption ROTATION_MODES[] = {
      { .label = "Keyframed", .tooltip = "The camera takes the rotation stored in the keys" },
      { .label = "Aim At Entity", .tooltip = "The camera turns toward the Aim Target; the keyed rotation is used while no "
                                             "entity has that name" },
    };

    float TrackDuration(const CameraTrackComponent& track)
    {
      return track.keys.empty() ? 0.0f : track.keys.back().time;
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

    float LowerBoundTime(const CameraTrackComponent& track, int index)
    {
      return index > 0 ? track.keys[index - 1].time + MIN_KEY_GAP : 0.0f;
    }

    float UpperBoundTime(const CameraTrackComponent& track, int index)
    {
      float lower = LowerBoundTime(track, index);
      if (index + 1 >= int(track.keys.size()))
        return std::max(lower, track.keys[index].time + MAX_VIEW_SPAN);
      return std::max(lower, track.keys[index + 1].time - MIN_KEY_GAP);
    }

    bool SameRotation(const glm::quat& a, const glm::quat& b)
    {
      return std::abs(a.x - b.x) <= ROTATION_EPSILON && std::abs(a.y - b.y) <= ROTATION_EPSILON
        && std::abs(a.z - b.z) <= ROTATION_EPSILON && std::abs(a.w - b.w) <= ROTATION_EPSILON;
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
    m_FramedTrack = entt::null;
    m_SelectedKey = -1;
    m_Playhead = 0.0f;
    m_Drag = DragKind::None;
    m_DragKey = -1;
    m_EulerTrack = entt::null;
    m_EulerKey = -1;
    context.sequencerTrack = entt::null;
    context.sequencerSelectedKey = -1;
    context.sequencerKeyPickRequest = -1;
    context.sequencerScrubRequest = -1.0f;
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

    if (m_Track == entt::null || !context.scene->HasComponent<CameraTrackComponent>(m_Track))
    {
      context.sequencerTrack = entt::null;
      context.sequencerSelectedKey = -1;
      ImGui::End();
      return;
    }

    auto& track = context.scene->GetComponent<CameraTrackComponent>(m_Track);

    if (m_FramedTrack != m_Track)
    {
      m_FramedTrack = m_Track;
      m_SelectedKey = -1;
      m_Playhead = 0.0f;
      m_ViewStart = 0.0f;
      m_ViewEnd = std::max(TrackDuration(track) * 1.1f, 5.0f);
    }

    if (m_SelectedKey >= int(track.keys.size()))
      m_SelectedKey = -1;

    // A key clicked in the viewport; adopted here because the panel republishes its own
    // selection into the context at the end of every render
    if (context.sequencerKeyPickRequest >= 0)
    {
      m_SelectedKey = std::min(context.sequencerKeyPickRequest, int(track.keys.size()) - 1);
      context.sequencerKeyPickRequest = -1;
    }

    // A viewport key drag already posed the camera; this keeps the playhead in step
    if (context.sequencerScrubRequest >= 0.0f)
    {
      SetPlayhead(context, track, context.sequencerScrubRequest);
      context.sequencerScrubRequest = -1.0f;
    }

    DrawTransportRow(context, track);
    DrawTimeline(context, track);
    DrawKeyInspector(context, track);
    DrawSettings(context, track);

    context.sequencerTrack = m_Track;
    context.sequencerSelectedKey = m_SelectedKey;

    ImGui::End();
  }

  void SequencerPanel::ResolveBinding(EditorContext& context)
  {
    Scene& scene = *context.scene;

    auto hasTrack = [&scene](Entity e) {
      return e != entt::null && scene.GetRegistry().valid(e)
        && scene.HasComponent<CameraTrackComponent>(e);
    };

    // Selecting a track entity binds it, but the binding then sticks: selecting the aim
    // target or a light to check something must not empty the panel mid-session.
    if (hasTrack(context.selectedEntity))
      m_Track = context.selectedEntity;
    else if (!hasTrack(m_Track))
      m_Track = entt::null;

    if (m_Track == entt::null)
      m_SelectedKey = -1;
  }

  void SequencerPanel::DrawBindingRow(EditorContext& context)
  {
    Scene& scene = *context.scene;
    entt::registry& registry = scene.GetRegistry();

    bool hasTracks = false;
    for (Entity candidate : scene.GetView<CameraTrackComponent>())
    {
      hasTracks = candidate != entt::null;
      break;
    }

    BeginPropertyScope();
    Entity picked = m_Track;
    const PropertyEdit trackEdit = PropertyEntity("Track", picked, scene, {
      .allowNone = false,
      .filter = [&registry](Entity candidate) { return registry.all_of<CameraTrackComponent>(candidate); },
      .tooltip = "Camera whose Camera Track the panel edits. Selecting a camera that has a track binds it as well.",
      .disabledReason = hasTracks ? nullptr : "No camera in the scene has a Camera Track" });
    EndPropertyScope();

    if (trackEdit.changed && picked != entt::null && picked != m_Track)
    {
      m_Track = picked;
      m_SelectedKey = -1;
      context.SelectEntity(picked);
    }

    Entity selected = context.selectedEntity;
    const bool canCreate = selected != entt::null && registry.valid(selected)
      && scene.HasComponent<CameraComponent>(selected)
      && !scene.HasComponent<CameraTrackComponent>(selected)
      && !scene.HasComponent<EditorOnlyTag>(selected);

    if (canCreate && InlineButton("Create Camera Track", {
      .icon = ICON_LC_CIRCLE_PLUS,
      .tooltip = "Adds a Camera Track to the selected camera and binds it" }))
    {
      scene.AddComponent<CameraTrackComponent>(selected);
      m_Track = selected;
      m_SelectedKey = -1;
    }

    if (m_Track == entt::null && !canCreate)
      ImGui::TextDisabled("Select a camera entity to author a flythrough track.");
  }

  void SequencerPanel::DrawTransportRow(EditorContext& context, CameraTrackComponent& track)
  {
    Scene& scene = *context.scene;
    CameraTrackPlayer* player = context.cameraTrackPlayer;
    float duration = TrackDuration(track);

    bool active = player != nullptr && player->IsPlaying() && player->GetTrackEntity() == m_Track;
    bool advancing = active && !player->IsPaused();
    if (advancing)
      m_Playhead = float(player->GetElapsed());

    if (advancing)
    {
      if (InlineButton("Pause", { .icon = ICON_LC_PAUSE }))
        player->Pause();
    }
    else
    {
      const char* playReason = player == nullptr ? "The editor has no camera track player"
        : track.keys.empty() ? "The track has no keys" : nullptr;
      if (InlineButton("Play", {
        .icon = ICON_LC_PLAY,
        .tooltip = "Play from the start; Pause + scrub + Play to preview a segment",
        .disabledReason = playReason }))
      {
        if (!EditorCommands::PlayCameraTrack(*player, scene, m_Track))
          m_Playhead = 0.0f;
      }
    }

    SameLineIfFits(ButtonWidth("Stop", ICON_LC_SQUARE));
    if (InlineButton("Stop", { .icon = ICON_LC_SQUARE, .disabledReason = active ? nullptr : "The track is not playing" }))
      player->Stop(scene);

    // Playback already hands the viewport to the track camera, so the two must not both
    // fight over the active camera
    const char* previewReason = active ? "Playback already shows the track camera" : nullptr;
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
      AddKeyFromView(context, track);
    }

    char status[64];
    std::snprintf(status, sizeof(status), "%.2f / %.2f s  (%d keys)", m_Playhead, duration, int(track.keys.size()));
    EditorFonts::Push(EditorFontRole::Mono);
    SameLineIfFits(ImGui::CalcTextSize(status).x);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(status);
    EditorFonts::Pop();
  }

  void SequencerPanel::DrawTimeline(EditorContext& context, CameraTrackComponent& track)
  {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float width = std::max(ImGui::GetContentRegionAvail().x, 64.0f);
    float height = RULER_HEIGHT + LANE_HEIGHT;
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
        int hit = -1;
        float best = KEY_GRAB_PX;
        if (mouse.y >= origin.y + RULER_HEIGHT)
        {
          for (size_t i = 0; i < track.keys.size(); i++)
          {
            float distance = std::abs(timeToX(track.keys[i].time) - mouse.x);
            if (distance <= best)
            {
              best = distance;
              hit = int(i);
            }
          }
        }

        if (hit >= 0)
        {
          m_SelectedKey = hit;
          m_DragKey = hit;
          m_Drag = DragKind::Key;
        }
        else
        {
          m_Drag = DragKind::Scrub;
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
        else if (m_Drag == DragKind::Key && m_DragKey >= 0 && m_DragKey < int(track.keys.size()))
        {
          // Clamped between the neighbours, so the sorted invariant holds without resorting
          track.keys[m_DragKey].time = glm::clamp(xToTime(mouse.x),
            LowerBoundTime(track, m_DragKey), UpperBoundTime(track, m_DragKey));
          SetPlayhead(context, track, track.keys[m_DragKey].time);
        }
        else if (m_Drag == DragKind::Scrub)
        {
          SetPlayhead(context, track, xToTime(mouse.x));
        }
      }
    }

    const EditorTheme& theme = EditorStyle::GetTheme();
    const auto themeColor = [](const glm::vec4& color) { return ImGui::GetColorU32(ToImGuiColor(color)); };

    draw->PushClipRect(origin, canvasEnd, true);

    draw->AddRectFilled(origin, canvasEnd, themeColor(theme.appBackground));
    draw->AddRectFilled(origin, ImVec2(canvasEnd.x, origin.y + RULER_HEIGHT), themeColor(theme.panel));

    float duration = TrackDuration(track);
    if (duration > 0.0f)
    {
      draw->AddRectFilled(ImVec2(timeToX(0.0f), origin.y + RULER_HEIGHT + 4.0f),
        ImVec2(timeToX(duration), canvasEnd.y - 4.0f), themeColor(theme.accentMuted));
    }

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

    float laneY = origin.y + RULER_HEIGHT + LANE_HEIGHT * 0.5f;
    for (size_t i = 0; i < track.keys.size(); i++)
    {
      float x = timeToX(track.keys[i].time);
      bool selected = int(i) == m_SelectedKey;
      ImVec2 points[4] = {
        ImVec2(x, laneY - KEY_HALF_SIZE), ImVec2(x + KEY_HALF_SIZE, laneY),
        ImVec2(x, laneY + KEY_HALF_SIZE), ImVec2(x - KEY_HALF_SIZE, laneY)
      };
      draw->AddConvexPolyFilled(points, 4, themeColor(selected ? theme.warning : theme.accent));
      draw->AddPolyline(points, 4, themeColor(theme.appBackground), ImDrawFlags_Closed, 1.5f);
    }

    float playheadX = timeToX(m_Playhead);
    draw->AddLine(ImVec2(playheadX, origin.y), ImVec2(playheadX, canvasEnd.y), themeColor(theme.error), 2.0f);

    draw->PopClipRect();
    draw->AddRect(origin, canvasEnd, themeColor(theme.borderSubtle));
  }

  void SequencerPanel::DrawKeyInspector(EditorContext& context, CameraTrackComponent& track)
  {
    if (!BeginPropertyGroup("Key", { .icon = ICON_LC_KEY, .tooltip = "The key selected in the timeline or in the viewport" }))
      return;

    if (m_SelectedKey < 0 || m_SelectedKey >= int(track.keys.size()))
    {
      PropertyStatus(nullptr, "Click a key in the timeline to edit it. Wheel zooms, Ctrl-drag pans.");
      EndPropertyGroup();
      return;
    }

    int index = m_SelectedKey;
    float lower = LowerBoundTime(track, index);
    float upper = UpperBoundTime(track, index);
    KeyAction action = KeyAction::None;

    {
      CameraTrackKey& key = track.keys[index];

      char position[32];
      std::snprintf(position, sizeof(position), "%d of %d", index + 1, int(track.keys.size()));
      PropertyReadOnly("Index", position, { .mono = true, .tooltip = "Position of the key on the track, counted from 1" });

      float time = key.time;
      if (PropertyFloat("Time", time, {
        .min = lower, .max = upper, .speed = 0.02f, .format = "%.3f", .unit = "s",
        .tooltip = "Seconds from the start of the track. Kept between the neighbouring keys, so the key order never "
                   "changes." }).changed)
      {
        // Explicit, since bounds that meet leave the row unclamped
        key.time = glm::clamp(time, lower, upper);
        SetPlayhead(context, track, key.time);
      }

      float fovDegrees = glm::degrees(key.fov);
      if (PropertyFloat("FOV", fovDegrees, {
        .min = MIN_FOV_DEGREES, .max = MAX_FOV_DEGREES, .speed = 0.25f, .format = "%.1f", .unit = "deg",
        .tooltip = "Vertical field of view at this key, eased toward the neighbouring keys in between." }).changed)
      {
        key.fov = glm::radians(glm::clamp(fovDegrees, MIN_FOV_DEGREES, MAX_FOV_DEGREES));
        SetPlayhead(context, track, m_Playhead);
      }

      if (PropertyVec3("Position", key.position, {
        .speed = 0.05f, .format = "%.2f", .unit = "m",
        .tooltip = "World-space camera position at this key." }).changed)
      {
        SetPlayhead(context, track, m_Playhead);
      }

      const bool cached = m_EulerTrack == m_Track && m_EulerKey == index && SameRotation(m_EulerRotation, key.rotation);
      glm::vec3 angles = cached ? m_EulerDegrees : glm::degrees(YawPitchRollFromRotation(key.rotation));
      if (PropertyVec3("Rotation", angles, {
        .speed = 0.5f, .format = "%.1f", .unit = "deg", .componentLabels = { "Yaw", "Pitch", "Roll" },
        .tooltip = "World-space camera rotation at this key: yaw about world up, then pitch, then roll about the view "
                   "direction. With Rotation Mode Aim At Entity it is used only while the Aim Target matches no entity." }).changed)
      {
        key.rotation = MakeYawPitchRollRotation(glm::radians(angles));
        SetPlayhead(context, track, m_Playhead);
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
          SetPlayhead(context, track, m_Playhead);
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
        SetPlayhead(context, track, copy.time);
        break;
      }
      case KeyAction::Delete:
      {
        track.keys.erase(track.keys.begin() + index);
        m_SelectedKey = -1;
        SetPlayhead(context, track, m_Playhead);
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

    float duration = TrackDuration(track);
    if (duration > m_ViewEnd)
      m_ViewEnd = duration * 1.1f;

    SetPlayhead(context, track, key.time);
  }

  void SequencerPanel::SetPlayhead(EditorContext& context, CameraTrackComponent& track, float time)
  {
    m_Playhead = glm::clamp(time, 0.0f, TrackDuration(track));

    CameraTrackPlayer* player = context.cameraTrackPlayer;
    if (player != nullptr && player->IsPlaying() && player->GetTrackEntity() == m_Track)
    {
      player->SetElapsed(*context.scene, double(m_Playhead));
      return;
    }

    // Stopped: the camera entity moves but the viewport does not follow it, so the frustum
    // and path gizmos show the result from wherever the editor camera is.
    if (!track.keys.empty())
      CameraTrackPlayer::ApplyTrackPose(*context.scene, m_Track, m_Playhead);
  }
}
