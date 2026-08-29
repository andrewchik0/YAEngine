#include "Editor/Panels/SequencerPanel.h"

#include <imgui.h>

#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorIcons.h"
#include "Scene/CameraTrackPlayer.h"

namespace YAEngine
{
  namespace
  {
    // Two keys at the same time make a zero-length segment the evaluator cannot use
    constexpr float MIN_KEY_GAP = 0.01f;
    constexpr float RULER_HEIGHT = 22.0f;
    constexpr float LANE_HEIGHT = 34.0f;
    constexpr float KEY_HALF_SIZE = 6.0f;
    constexpr float KEY_GRAB_PX = 9.0f;
    // Labels are ~40px wide, so this is what keeps them from colliding at any zoom
    constexpr float MIN_LABEL_SPACING_PX = 64.0f;
    constexpr float MIN_VIEW_SPAN = 0.25f;
    constexpr float MAX_VIEW_SPAN = 3600.0f;
    constexpr float DUPLICATE_OFFSET = 0.5f;

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
  }

  void SequencerPanel::OnSceneReady(EditorContext& context)
  {
    m_Track = entt::null;
    m_FramedTrack = entt::null;
    m_SelectedKey = -1;
    m_Playhead = 0.0f;
    m_Drag = DragKind::None;
    m_DragKey = -1;
    context.sequencerTrack = entt::null;
    context.sequencerSelectedKey = -1;
    context.sequencerKeyPickRequest = -1;
    context.sequencerScrubRequest = -1.0f;
  }

  void SequencerPanel::OnRender(EditorContext& context)
  {
    if (!ImGui::Begin("Sequencer"))
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

    ImGui::Separator();
    DrawSettingsRow(track);
    ImGui::Separator();
    DrawTransportRow(context, track);
    DrawTimeline(context, track);
    ImGui::Separator();
    DrawKeyInspector(context, track);

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

    std::vector<Entity> tracks;
    for (auto e : scene.GetView<CameraTrackComponent>())
      tracks.push_back(e);

    if (!tracks.empty())
    {
      const char* preview = m_Track != entt::null ? scene.GetName(m_Track).c_str() : "None";
      ImGui::SetNextItemWidth(200.0f);
      if (ImGui::BeginCombo(ICON_FA_FILM " Track", preview))
      {
        for (Entity e : tracks)
        {
          ImGui::PushID(int(entt::to_integral(e)));
          if (ImGui::Selectable(scene.GetName(e).c_str(), e == m_Track))
          {
            m_Track = e;
            m_SelectedKey = -1;
            context.SelectEntity(e);
          }
          ImGui::PopID();
        }
        ImGui::EndCombo();
      }
    }

    Entity selected = context.selectedEntity;
    bool canCreate = selected != entt::null && scene.GetRegistry().valid(selected)
      && scene.HasComponent<CameraComponent>(selected)
      && !scene.HasComponent<CameraTrackComponent>(selected)
      && !scene.HasComponent<EditorOnlyTag>(selected);

    if (canCreate)
    {
      if (!tracks.empty())
        ImGui::SameLine();
      if (ImGui::Button(ICON_FA_CIRCLE_PLUS " Create Camera Track"))
      {
        scene.AddComponent<CameraTrackComponent>(selected);
        m_Track = selected;
        m_SelectedKey = -1;
      }
    }

    if (m_Track == entt::null && !canCreate)
      ImGui::TextDisabled("Select a camera entity to author a flythrough track.");
  }

  void SequencerPanel::DrawSettingsRow(CameraTrackComponent& track)
  {
    static const char* ROTATION_MODES[] = { "Keyframed", "Aim At Entity" };

    int mode = int(track.rotationMode);
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::Combo("Rotation", &mode, ROTATION_MODES, IM_ARRAYSIZE(ROTATION_MODES)))
      track.rotationMode = CameraTrackComponent::RotationMode(uint8_t(mode));

    if (track.rotationMode == CameraTrackComponent::RotationMode::AimAt)
    {
      ImGui::SameLine();
      char buffer[128];
      std::snprintf(buffer, sizeof(buffer), "%s", track.aimTargetName.c_str());
      ImGui::SetNextItemWidth(160.0f);
      if (ImGui::InputText("Aim Target", buffer, sizeof(buffer)))
        track.aimTargetName = buffer;
    }

    ImGui::SameLine();
    ImGui::Checkbox("Reset PostFX On Start", &track.resetPostFXOnStart);
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
      if (ImGui::Button(ICON_FA_PAUSE " Pause"))
        player->Pause();
    }
    else
    {
      ImGui::BeginDisabled(player == nullptr || track.keys.empty());
      if (ImGui::Button(ICON_FA_PLAY " Play"))
      {
        // Resuming exactly at the end would stop again on the next tick
        bool resumable = active && player->GetElapsed() < double(duration) - 1e-4;
        if (resumable)
        {
          player->Resume();
        }
        else
        {
          if (active)
            player->Stop(scene);
          player->Start(scene, m_Track);
          m_Playhead = 0.0f;
        }
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Play from the start; Pause + scrub + Play to preview a segment");
      ImGui::EndDisabled();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!active);
    if (ImGui::Button(ICON_FA_STOP " Stop"))
      player->Stop(scene);
    ImGui::EndDisabled();

    ImGui::SameLine();
    // Playback already hands the viewport to the track camera, so the two must not both
    // fight over the active camera
    ImGui::BeginDisabled(active);
    if (context.previewCamera == m_Track)
    {
      if (ImGui::Button(ICON_FA_XMARK " Stop Preview"))
        context.StopCameraPreview();
    }
    else if (ImGui::Button(ICON_FA_VIDEO " Preview"))
    {
      context.StartCameraPreview(m_Track);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_CIRCLE_PLUS " Add Key"))
      AddKeyFromView(context, track);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Captures the editor camera pose at the playhead, with the track camera fov");

    ImGui::SameLine();
    ImGui::Text("%.2f / %.2f s  (%d keys)", m_Playhead, duration, int(track.keys.size()));
  }

  void SequencerPanel::DrawTimeline(EditorContext& context, CameraTrackComponent& track)
  {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float width = std::max(ImGui::GetContentRegionAvail().x, 64.0f);
    float height = RULER_HEIGHT + LANE_HEIGHT;
    ImVec2 canvasEnd(origin.x + width, origin.y + height);

    ImGui::InvisibleButton("##sequencerTimeline", ImVec2(width, height),
      ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);

    // Members are read live, so a pan applied this frame maps correctly right away
    auto timeToX = [this, origin, width](float t) {
      float span = std::max(m_ViewEnd - m_ViewStart, 1e-4f);
      return origin.x + (t - m_ViewStart) / span * width;
    };
    auto xToTime = [this, origin, width](float x) {
      float span = std::max(m_ViewEnd - m_ViewStart, 1e-4f);
      return m_ViewStart + (x - origin.x) / width * span;
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
          m_ViewStart = m_PanAnchorTime - (mouse.x - origin.x) / width * span;
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

    draw->PushClipRect(origin, canvasEnd, true);

    draw->AddRectFilled(origin, canvasEnd, IM_COL32(28, 28, 32, 255));
    draw->AddRectFilled(origin, ImVec2(canvasEnd.x, origin.y + RULER_HEIGHT), IM_COL32(38, 38, 44, 255));

    float duration = TrackDuration(track);
    if (duration > 0.0f)
    {
      draw->AddRectFilled(ImVec2(timeToX(0.0f), origin.y + RULER_HEIGHT + 4.0f),
        ImVec2(timeToX(duration), canvasEnd.y - 4.0f), IM_COL32(46, 52, 62, 255));
    }

    float step = NiceTickStep(m_ViewEnd - m_ViewStart, width);
    // Panning past the start would otherwise label negative seconds
    float first = std::max(std::ceil(m_ViewStart / step) * step, 0.0f);
    int tickCount = int((m_ViewEnd - first) / step) + 1;
    for (int i = 0; i < tickCount && i < 256; i++)
    {
      float t = first + step * float(i);
      float x = timeToX(t);
      draw->AddLine(ImVec2(x, origin.y), ImVec2(x, canvasEnd.y), IM_COL32(58, 58, 68, 255));

      char label[24];
      std::snprintf(label, sizeof(label), step < 1.0f ? "%.2f" : "%.1f", t);
      draw->AddText(ImVec2(x + 3.0f, origin.y + 3.0f), IM_COL32(150, 150, 160, 255), label);
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
      draw->AddConvexPolyFilled(points, 4,
        selected ? IM_COL32(255, 191, 64, 255) : IM_COL32(115, 153, 191, 255));
      draw->AddPolyline(points, 4, IM_COL32(18, 18, 22, 255), ImDrawFlags_Closed, 1.5f);
    }

    float playheadX = timeToX(m_Playhead);
    draw->AddLine(ImVec2(playheadX, origin.y), ImVec2(playheadX, canvasEnd.y),
      IM_COL32(240, 90, 70, 255), 2.0f);

    draw->PopClipRect();
    draw->AddRect(origin, canvasEnd, IM_COL32(70, 70, 80, 255));
  }

  void SequencerPanel::DrawKeyInspector(EditorContext& context, CameraTrackComponent& track)
  {
    if (m_SelectedKey < 0 || m_SelectedKey >= int(track.keys.size()))
    {
      ImGui::TextDisabled("Click a key in the timeline to edit it. Wheel zooms, Ctrl-drag pans.");
      return;
    }

    int index = m_SelectedKey;
    float lower = LowerBoundTime(track, index);
    float upper = UpperBoundTime(track, index);
    KeyAction action = KeyAction::None;

    ImGui::Text("Key %d", index);

    {
      CameraTrackKey& key = track.keys[index];

      float time = key.time;
      ImGui::SetNextItemWidth(120.0f);
      if (ImGui::DragFloat("Time", &time, 0.02f, lower, upper, "%.3f s"))
      {
        key.time = glm::clamp(time, lower, upper);
        SetPlayhead(context, track, key.time);
      }

      float fovDegrees = glm::degrees(key.fov);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(120.0f);
      if (ImGui::DragFloat("FOV", &fovDegrees, 0.25f, 10.0f, 120.0f, "%.1f deg"))
      {
        key.fov = glm::radians(glm::clamp(fovDegrees, 10.0f, 120.0f));
        SetPlayhead(context, track, m_Playhead);
      }

      if (ImGui::DragFloat3("Position", &key.position.x, 0.05f))
        SetPlayhead(context, track, m_Playhead);

      if (ImGui::Button(ICON_FA_CAMERA " Update From View"))
        action = KeyAction::UpdateFromView;
      ImGui::SameLine();
      if (ImGui::Button(ICON_FA_CLONE " Duplicate"))
        action = KeyAction::Duplicate;
      ImGui::SameLine();
      if (ImGui::Button(ICON_FA_TRASH " Delete"))
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
