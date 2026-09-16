#include "Editor/EditorLayer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include "Input/InputSystem.h"
#include "LayerManager.h"
#include "Window.h"
#include "Editor/Utils/EditorStyle.h"
#include "Editor/Utils/EditorFonts.h"
#include "Editor/Utils/EditorWidgets.h"
#include "Editor/Panels/ViewportPanel.h"
#include "Editor/Panels/PerformancePanel.h"
#include "Editor/Panels/RenderSettingsPanel.h"
#include "Editor/Panels/OutlinerPanel.h"
#include "Editor/Panels/DetailsPanel.h"
#include "Editor/Panels/MaterialBrowserPanel.h"
#include "Editor/Panels/MaterialInspectorPanel.h"
#include "Editor/Panels/SequencerPanel.h"
#include "Editor/Panels/AgentPanel.h"
#include "Editor/Panels/DeveloperPanel.h"
#include "Editor/EditorCameraLayer.h"
#include "Editor/Utils/FileDialog.h"

#include "Assets/AssetManager.h"
#include "Render/Render.h"
#include "Utils/Projection.h"
#include "Scene/SceneSerializer.h"
#include "Scene/SequencePlayer.h"
#include "Scene/ComponentRegistry.h"
#include "Utils/ServiceRegistry.h"
#include "Utils/ThreadPool.h"
#include "Utils/Ray.h"
#include "Scene/SystemScheduler.h"

#include <glm/gtc/matrix_transform.hpp>

namespace YAEngine
{
  namespace
  {
    // Keeps each imgui.ini's default layout version; defined next to the layout code below
    void RegisterLayoutSettingsHandler();
  }

  static float QueryWindowContentScale(GLFWwindow* window)
  {
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    glfwGetWindowContentScale(window, &scaleX, &scaleY);
    return scaleX > 0.0f ? scaleX : 1.0f;
  }

  template<typename TPanel, typename... TArgs>
  TPanel& EditorLayer::AddPanel(TArgs&&... args)
  {
    auto panel = std::make_unique<TPanel>(std::forward<TArgs>(args)...);
    const EditorPanelDescriptor& descriptor = panel->GetDescriptor();
    auto stored = m_Preferences.panelVisibility.find(descriptor.name);
    panel->SetVisible(stored != m_Preferences.panelVisibility.end() ? stored->second : descriptor.defaultVisible);

    TPanel& added = *panel;
    m_Panels.push_back(std::move(panel));
    return added;
  }

  void EditorLayer::OnAttach()
  {
    FileDialog::Init();
    RegisterLayoutSettingsHandler();

    m_Preferences.Load();
    m_ContentScale = QueryWindowContentScale(GetWindow().Get());
    EditorStyle::Apply(m_Preferences.theme, m_ContentScale);
    EditorFonts::Load(m_Preferences.theme);
    EditorWidgets::SetPreferences(&m_Preferences);

    const EditorPreferenceOverrides& overrides = m_Registry->Get<EditorPreferenceOverrides>();
    m_Bridge.Init(*m_Registry, overrides.mcpEnabled.value_or(m_Preferences.mcpEnabled));
    RegisterBridgeActions();

    GetLayerManager().PushLayer<EditorCameraLayer>();
    // Also the order of the View menu entries and of the tabs that share a dock node
    m_ViewportPanel = &AddPanel<ViewportPanel>(m_Preferences, GetLayerManager().GetLayer<EditorCameraLayer>());
    m_OutlinerPanel = &AddPanel<OutlinerPanel>();
    m_DetailsPanel = &AddPanel<DetailsPanel>();
    AddPanel<RenderSettingsPanel>();
    AddPanel<MaterialBrowserPanel>();
    MaterialInspectorPanel& materialInspector = AddPanel<MaterialInspectorPanel>();
    SequencerPanel& sequencer = AddPanel<SequencerPanel>();
    AddPanel<PerformancePanel>();
    m_AgentPanel = &AddPanel<AgentPanel>(m_Bridge, m_Preferences, overrides.mcpEnabled);
    AddPanel<DeveloperPanel>(m_Preferences);

    m_DetailsPanel->LinkPanels(materialInspector, sequencer, [this](IEditorPanel& panel)
    {
      SetPanelVisible(panel, true);
      panel.RequestFocus();
    });
  }

  void EditorLayer::OnSceneReady()
  {
    m_Context.scene = &GetScene();
    m_Context.assetManager = &GetAssets();
    m_Context.render = &GetRender();
    m_Context.timer = &GetTimer();
    m_Context.componentRegistry = &m_Registry->Get<ComponentRegistry>();
    m_Context.sequencePlayer = &m_Registry->Get<SequencePlayer>();
    m_Context.captureSession = &m_Registry->Get<FrameCaptureSessionResult>();
    m_Context.bridgeCapture = &m_Registry->Get<BridgeCaptureStatus>();

    m_TextureCache.Init(m_Context.assetManager);
    m_Context.textureCache = &m_TextureCache;

    if (m_CurrentScenePath.empty())
      m_CurrentScenePath = GetScene().GetScenePath();

    for (auto& panel : m_Panels)
      panel->OnSceneReady(m_Context);

    m_Bridge.OnSceneReady(m_CurrentScenePath);
  }

  void EditorLayer::LateUpdate(double deltaTime)
  {
    m_Bridge.LateUpdate();
  }

  static glm::vec3 AxisToDirection(GizmoAxis axis)
  {
    switch (axis)
    {
      case GizmoAxis::X: return { 1, 0, 0 };
      case GizmoAxis::Y: return { 0, 1, 0 };
      case GizmoAxis::Z: return { 0, 0, 1 };
      default: return { 0, 0, 0 };
    }
  }

  static glm::vec3 ComputeDragPlaneNormal(const glm::vec3& axisDir, const glm::vec3& camDir)
  {
    // Plane that contains the drag axis and faces the camera as much as possible
    glm::vec3 projected = camDir - glm::dot(camDir, axisDir) * axisDir;
    float len = glm::length(projected);
    if (len < 1e-4f)
    {
      // Camera looking along the axis - pick an arbitrary perpendicular
      glm::vec3 up = (std::abs(axisDir.y) < 0.9f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
      return glm::normalize(glm::cross(axisDir, up));
    }
    return projected / len;
  }

  void EditorLayer::Update(double deltaTime)
  {
    EditorStyle::ApplyRequestedTheme();

    // The node overlay runs from the gizmo callback, which the renderer skips while gizmos are off.
    if (!m_VolumeNodeCachePath.empty() && m_Context.render && !m_Context.render->GetGizmosEnabled())
      ReleaseVolumeNodeCache();

    // Moving the window to a monitor with another scale factor rescales the whole UI
    float contentScale = QueryWindowContentScale(GetWindow().Get());
    if (contentScale != m_ContentScale)
    {
      m_ContentScale = contentScale;
      EditorStyle::Apply(EditorStyle::GetTheme(), contentScale);
    }

    // Before the scene switch: a bake asked for in the same frame belongs to the scene it was asked in.
    if (m_Context.bakeAllVolumesRequest)
    {
      m_Context.bakeAllVolumesRequest = false;
      m_Context.volumeBakeRequest = entt::null;
      GetRender().BakeAllIrradianceVolumes(GetScene(), GetAssets());
    }
    else if (m_Context.volumeBakeRequest != entt::null)
    {
      Entity volume = m_Context.volumeBakeRequest;
      m_Context.volumeBakeRequest = entt::null;
      if (GetScene().GetRegistry().valid(volume))
        GetRender().BakeIrradianceVolume(volume, GetScene(), GetAssets());
    }

    if (b_PendingNewScene)
    {
      b_PendingNewScene = false;
      NewScene();
    }
    else if (!m_PendingScenePath.empty())
    {
      LoadSceneDeferred(m_PendingScenePath);
      m_PendingScenePath.clear();
    }

    // Deleting the previewed camera would leave the viewport on a dead entity
    if (m_Context.IsPreviewingCamera()
      && (!GetScene().GetRegistry().valid(m_Context.previewCamera)
        || !GetScene().HasComponent<CameraComponent>(m_Context.previewCamera)))
    {
      m_Context.StopCameraPreview();
    }

    // Entities die through the outliner, the bridge and model reloads alike, so previews of
    // gone ones are dropped here rather than on each of those paths.
    GetRender().PruneIrradianceVolumePlacementPreviews(GetScene());

    uint32_t w = m_Context.viewportWidth;
    uint32_t h = m_Context.viewportHeight;
    if (w > 0 && h > 0 && (w != m_LastViewportWidth || h != m_LastViewportHeight))
    {
      m_LastViewportWidth = w;
      m_LastViewportHeight = h;
      for (auto [entity, cam] : GetScene().GetView<CameraComponent>().each())
        cam.Resize(float(w), float(h));
    }

    auto& input = GetInput();

    // While the cursor is captured its reported position runs off into the distance, and
    // ImGui would light up panels under a pointer that is not there. The GLFW backend
    // leaves the cursor itself alone in this mode, so only the position needs suppressing.
    ImGuiIO& io = ImGui::GetIO();
    if (input.IsMouseCaptured())
      io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    else
      io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;

    auto& gizmo = m_Context.render->GetGizmoRenderer();
    gizmo.SetSpriteMaxPixels(GizmoRenderer::SPRITE_MAX_PIXELS * m_ContentScale);

    // Blender-style: grabbing the viewport while looking through a scene camera hands
    // control back to the editor camera instead of locking navigation
    if (m_Context.IsPreviewingCamera() && m_Context.viewportHovered && !b_DragActive
      && (input.IsMousePressed(MouseButton::Right) || input.IsMousePressed(MouseButton::Middle)
        || input.IsKeyPressed(Key::W) || input.IsKeyPressed(Key::A)
        || input.IsKeyPressed(Key::S) || input.IsKeyPressed(Key::D)))
    {
      m_Context.StopCameraPreview();
    }

    if (m_Context.viewportHovered && !b_DragActive)
    {
      if (input.IsKeyPressed(Key::D1)) m_Context.render->GetGizmoMode() = GizmoMode::Translate;
      if (input.IsKeyPressed(Key::D2)) m_Context.render->GetGizmoMode() = GizmoMode::Rotate;
      if (input.IsKeyPressed(Key::D3)) m_Context.render->GetGizmoMode() = GizmoMode::Scale;
    }

    // Id picks are answered a few frames after the click was sent to the renderer
    if (b_PickRequestActive)
    {
      PickResult pickResult {};
      if (m_Context.render->ConsumePickResult(pickResult))
      {
        b_PickRequestActive = false;
        ApplyPickResult(pickResult);
      }
    }

    Ray viewportRay {};
    glm::mat4 viewportView { 1.0f };
    glm::mat4 viewportProj { 1.0f };
    bool hasViewportRay = false;

    if (m_Context.mouseInViewportValid)
    {
      Entity camEntity = GetScene().GetActiveCamera();
      if (camEntity != entt::null && GetScene().HasComponent<CameraComponent>(camEntity))
      {
        auto& camTransform = GetScene().GetTransform(camEntity);
        auto& cam = GetScene().GetComponent<CameraComponent>(camEntity);

        glm::mat4 world = glm::translate(glm::mat4(1.0f), camTransform.position)
                        * glm::mat4_cast(camTransform.rotation);
        glm::mat4 view = glm::inverse(world);
        glm::mat4 proj = MakeReversedInfinitePerspective(cam.fov, cam.aspectRatio, cam.nearPlane);

        viewportRay = ScreenToRay(m_Context.mouseInViewport, glm::inverse(proj), glm::inverse(view));
        viewportView = view;
        viewportProj = proj;
        hasViewportRay = true;
      }
    }

    if (b_DragActive)
    {
      if (input.IsMouseReleased(MouseButton::Left))
      {
        b_DragActive = false;
        b_DragTargetKey = false;
        b_DragTargetPoint = false;
        gizmo.SetDraggedAxis(GizmoAxis::None);
        input.SetGizmoDragging(false);
      }
      else if (hasViewportRay)
      {
        auto hitT = RayPlaneIntersect(viewportRay, m_DragStartWorldPos, m_DragPlaneNormal);
        if (hitT)
        {
          glm::vec3 currentHit = viewportRay.origin + *hitT * viewportRay.direction;
          glm::vec3 delta = currentHit - m_DragStartHitPoint;

          if (b_DragTargetKey)
          {
            DragSequencerKey(delta, currentHit);
          }
          else if (b_DragTargetPoint)
          {
            DragPathPoint(delta);
          }
          else
          {
            Entity entity = m_Context.selectedEntity;
            auto& scene = *m_Context.scene;
            auto& localTransform = scene.GetTransform(entity);

            auto& hierarchy = scene.GetHierarchy(entity);
            bool hasParent = hierarchy.parent != entt::null
                          && scene.HasComponent<WorldTransform>(hierarchy.parent);
            glm::mat4 parentWorldInv(1.0f);
            if (hasParent)
              parentWorldInv = glm::inverse(scene.GetComponent<WorldTransform>(hierarchy.parent).world);

            if (m_DragMode == GizmoMode::Translate)
            {
              float projectedDist = glm::dot(delta, m_DragAxisDir);
              glm::vec3 worldDelta = projectedDist * m_DragAxisDir;
              glm::vec3 newWorldPos = m_DragStartWorldPos + worldDelta;

              if (hasParent)
                localTransform.position = glm::vec3(parentWorldInv * glm::vec4(newWorldPos, 1.0f));
              else
                localTransform.position = newWorldPos;
            }
            else if (m_DragMode == GizmoMode::Rotate)
            {
              glm::vec3 v1 = m_DragStartHitPoint - m_DragStartWorldPos;
              glm::vec3 v2 = currentHit - m_DragStartWorldPos;

              float l1 = glm::length(v1);
              float l2 = glm::length(v2);
              if (l1 > 1e-6f && l2 > 1e-6f)
              {
                v1 /= l1;
                v2 /= l2;
                float cosAngle = glm::clamp(glm::dot(v1, v2), -1.0f, 1.0f);
                float sinAngle = glm::dot(glm::cross(v1, v2), m_DragAxisDir);
                float angle = std::atan2(sinAngle, cosAngle);

                glm::quat worldRotDelta = glm::angleAxis(angle, m_DragAxisDir);

                if (hasParent)
                {
                  glm::quat parentRot = glm::quat_cast(glm::mat3(
                    scene.GetComponent<WorldTransform>(hierarchy.parent).world));
                  localTransform.rotation = glm::normalize(
                    glm::inverse(parentRot) * worldRotDelta * parentRot
                    * m_DragStartLocalTransform.rotation);
                }
                else
                {
                  localTransform.rotation = glm::normalize(
                    worldRotDelta * m_DragStartLocalTransform.rotation);
                }
              }
            }
            else if (m_DragMode == GizmoMode::Scale)
            {
              float projectedDist = glm::dot(delta, m_DragAxisDir);
              float scaleFactor = 1.0f + projectedDist / m_DragGizmoScale;
              scaleFactor = glm::max(scaleFactor, 0.01f);

              int axisIdx = static_cast<int>(m_DragAxis) - 1;
              localTransform.scale[axisIdx] =
                m_DragStartLocalTransform.scale[axisIdx] * scaleFactor;
            }

            scene.MarkDirty(entity);
          }
        }
      }
    }
    else
    {
      if (hasViewportRay && m_Context.selectedEntity != entt::null && m_Context.viewportHovered)
        gizmo.UpdateHover(viewportRay);
      else
        gizmo.ClearHover();

      if (input.IsMousePressed(MouseButton::Left) && hasViewportRay)
      {
        GizmoAxis hoveredAxis = gizmo.GetHoveredAxis();

        if (hoveredAxis != GizmoAxis::None && m_Context.selectedEntity != entt::null)
        {
          Entity entity = m_Context.selectedEntity;
          auto& scene = *m_Context.scene;
          GizmoMode mode = m_Context.render->GetGizmoMode();

          // Scale has no meaning for a track key, and only translation for a path point; the
          // click is swallowed rather than transforming the entity under a sub-object gizmo
          CameraTrackKey* key = ActiveSequencerKey();
          glm::vec3* point = key == nullptr ? ActivePathPoint() : nullptr;
          bool scaleOnKey = key != nullptr && mode == GizmoMode::Scale;
          bool turnOnPoint = point != nullptr && mode != GizmoMode::Translate;
          if (!scaleOnKey && !turnOnPoint)
          {
            glm::vec3 gizmoPos;
            if (key != nullptr)
            {
              gizmoPos = key->position;
              m_DragStartLocalTransform = LocalTransform {};
              m_DragStartLocalTransform.position = key->position;
              m_DragStartLocalTransform.rotation = key->rotation;
            }
            else if (point != nullptr)
            {
              gizmoPos = *point;
              m_DragStartLocalTransform = LocalTransform {};
              m_DragStartLocalTransform.position = *point;
            }
            else
            {
              auto& wt = scene.GetComponent<WorldTransform>(entity);
              gizmoPos = glm::vec3(wt.world[3]);
              m_DragStartLocalTransform = scene.GetTransform(entity);
            }

            b_DragTargetKey = key != nullptr;
            b_DragTargetPoint = point != nullptr;
            m_DragAxis = hoveredAxis;
            m_DragMode = mode;
            m_DragStartWorldPos = gizmoPos;
            m_DragAxisDir = AxisToDirection(hoveredAxis);

            Entity camEntity = GetScene().GetActiveCamera();
            glm::vec3 camPos = GetScene().GetTransform(camEntity).position;

            if (m_DragMode == GizmoMode::Rotate)
            {
              m_DragPlaneNormal = m_DragAxisDir;
            }
            else
            {
              glm::vec3 camDir = glm::normalize(camPos - gizmoPos);
              m_DragPlaneNormal = ComputeDragPlaneNormal(m_DragAxisDir, camDir);
            }

            float dist = glm::length(camPos - gizmoPos);
            m_DragGizmoScale = dist * 0.15f;

            auto hitT = RayPlaneIntersect(viewportRay, m_DragStartWorldPos, m_DragPlaneNormal);
            if (hitT)
            {
              m_DragStartHitPoint = viewportRay.origin + *hitT * viewportRay.direction;
              b_DragActive = true;
              gizmo.SetDraggedAxis(hoveredAxis);
              input.SetGizmoDragging(true);
            }
          }
        }
        else
        {
          // Track keys first: they are the smallest targets on screen and sit on top of
          // the path the user is editing. Then icons - they are drawn as an unoccluded
          // overlay and never reach the id buffer, so anything else winning over them
          // would make a visible icon unclickable. An icon always selects its own
          // entity, with no root walk.
          Entity keyTrack = entt::null;
          int keyIndex = -1;
          Entity pointPath = entt::null;
          int pointIndex = -1;
          Entity icon = entt::null;
          if (PickTrackKey(viewportRay, keyTrack, keyIndex))
          {
            b_PickRequestActive = false;
            m_Context.SelectEntity(keyTrack);
            m_Context.sequencerTrack = keyTrack;
            m_Context.sequencerSelectedKey = keyIndex;
            m_Context.sequencerKeyPickRequest = keyIndex;
          }
          else if (PickPathPoint(viewportRay, pointPath, pointIndex))
          {
            b_PickRequestActive = false;
            m_Context.SelectEntity(pointPath);
            m_Context.sequencerPath = pointPath;
            m_Context.sequencerSelectedPoint = pointIndex;
            m_Context.sequencerPointPickRequest = pointIndex;
          }
          else if ((icon = PickIconEntity(viewportRay, viewportView, viewportProj)) != entt::null)
          {
            b_PickRequestActive = false;
            m_Context.SelectEntity(icon);
          }
          else
          {
            // Geometry goes through the id buffer. The renderer rasterizes entity ids
            // into this pixel and the answer comes back a few frames later, so what the
            // click meant is stored until then - the modifier especially, it will long
            // have been released by the time the result lands.
            m_Context.render->RequestPick(m_Context.mouseInViewport);
            b_PickRequestActive = true;
            b_PickRequestExact = input.IsKeyDown(Key::LeftControl) || input.IsKeyDown(Key::RightControl);
            m_PickFallbackRay = viewportRay;
          }
        }
      }
    }

    // The gizmo (and its hover test) anchors to the selected sequencer key when there is
    // one, and to the selected entity otherwise
    if (CameraTrackKey* anchorKey = ActiveSequencerKey())
    {
      m_Context.render->SetSelectedEntityPosition(anchorKey->position);
    }
    else if (glm::vec3* anchorPoint = ActivePathPoint())
    {
      m_Context.render->SetSelectedEntityPosition(*anchorPoint);
    }
    else if (m_Context.selectedEntity != entt::null && m_Context.scene->HasComponent<WorldTransform>(m_Context.selectedEntity))
    {
      auto& t = m_Context.scene->GetComponent<WorldTransform>(m_Context.selectedEntity);
      glm::vec3 pos(t.world[3]);
      m_Context.render->SetSelectedEntityPosition(pos);
    }
    else
    {
      m_Context.render->ClearSelectedEntity();
    }
  }

  namespace
  {
    // Bump whenever BuildDefaultLayout changes: every imgui.ini then has its docking rebuilt once
    constexpr uint32_t EDITOR_LAYOUT_VERSION = 2;

    constexpr const char* LAYOUT_SETTINGS_TYPE = "YAEngineLayout";
    constexpr const char* LAYOUT_SETTINGS_ENTRY = "Data";

    // Default layout version the docking of the loaded imgui.ini was built from; 0 for an ini without one.
    // Stored in imgui.ini itself because docking is per ini, one per working directory. Not a layer member:
    // ImGui writes the ini once more when its context is destroyed, after the layer is gone.
    uint32_t s_IniLayoutVersion = 0;

    void ParseLayoutSettingsLine(std::string_view line)
    {
      constexpr std::string_view KEY = "Version=";
      if (!line.starts_with(KEY))
        return;

      const std::string value(line.substr(KEY.size()));
      char* end = nullptr;
      const auto parsed = std::strtoul(value.c_str(), &end, 10);
      if (end != value.c_str())
        s_IniLayoutVersion = uint32_t(parsed);
    }

    void ReadLayoutVersionFromIniFile(const char* fileName)
    {
      const std::string header = std::string("[") + LAYOUT_SETTINGS_TYPE + "][" + LAYOUT_SETTINGS_ENTRY + "]";
      std::ifstream file(fileName, std::ios::binary);
      std::string line;
      bool inEntry = false;
      s_IniLayoutVersion = 0;
      while (std::getline(file, line))
      {
        if (!line.empty() && line.back() == '\r')
          line.pop_back();
        if (line.starts_with('['))
          inEntry = line == header;
        else if (inEntry)
          ParseLayoutSettingsLine(line);
      }
    }

    void RegisterLayoutSettingsHandler()
    {
      if (ImGui::FindSettingsHandler(LAYOUT_SETTINGS_TYPE) == nullptr)
      {
        ImGuiSettingsHandler handler;
        handler.TypeName = LAYOUT_SETTINGS_TYPE;
        handler.TypeHash = ImHashStr(LAYOUT_SETTINGS_TYPE);
        handler.ReadInitFn = [](ImGuiContext*, ImGuiSettingsHandler*) { s_IniLayoutVersion = 0; };
        handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char* name) -> void* {
          return std::strcmp(name, LAYOUT_SETTINGS_ENTRY) == 0 ? &s_IniLayoutVersion : nullptr;
        };
        handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line) {
          ParseLayoutSettingsLine(line);
        };
        handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* self, ImGuiTextBuffer* out) {
          if (s_IniLayoutVersion != 0)
            out->appendf("[%s][%s]\nVersion=%u\n\n", self->TypeName, LAYOUT_SETTINGS_ENTRY, s_IniLayoutVersion);
        };
        ImGui::AddSettingsHandler(&handler);
      }

      // A frame drawn before the editor attached may already have read imgui.ini without this handler
      const ImGuiContext& g = *ImGui::GetCurrentContext();
      if (g.SettingsLoaded && g.IO.IniFilename != nullptr)
        ReadLayoutVersionFromIniFile(g.IO.IniFilename);
    }
  }

  void EditorLayer::SetPanelVisible(IEditorPanel& panel, bool visible)
  {
    if (panel.IsVisible() == visible)
      return;

    panel.SetVisible(visible);
    StorePanelVisibility(panel);
    m_Preferences.Save();
  }

  void EditorLayer::StorePanelVisibility(const IEditorPanel& panel)
  {
    const EditorPanelDescriptor& descriptor = panel.GetDescriptor();
    if (panel.IsVisible() == descriptor.defaultVisible)
      m_Preferences.panelVisibility.erase(descriptor.name);
    else
      m_Preferences.panelVisibility[descriptor.name] = panel.IsVisible();
  }

  void EditorLayer::DrawViewMenu()
  {
    static constexpr const char* CATEGORY_NAMES[] = { "Scene", "Assets", "Rendering", "Animation", "Tools" };
    static_assert(std::size(CATEGORY_NAMES) == size_t(EditorPanelCategory::Count));

    auto drawEntry = [this](IEditorPanel& panel)
    {
      const bool visible = panel.IsVisible();
      if (ImGui::MenuItem(panel.GetName(), nullptr, visible))
      {
        SetPanelVisible(panel, !visible);
        if (!visible)
          panel.RequestFocus();
      }
    };

    for (size_t category = 0; category < std::size(CATEGORY_NAMES); category++)
    {
      EditorWidgets::PropertySubHeading(CATEGORY_NAMES[category]);
      for (auto& panel : m_Panels)
      {
        const EditorPanelDescriptor& descriptor = panel->GetDescriptor();
        if (!descriptor.developer && size_t(descriptor.category) == category)
          drawEntry(*panel);
      }
    }

    ImGui::Separator();
    for (auto& panel : m_Panels)
    {
      if (panel->GetDescriptor().developer)
        drawEntry(*panel);
    }

    ImGui::Separator();
    if (ImGui::BeginMenu("Theme"))
    {
      DrawThemeMenu();
      ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Reset Layout"))
    {
      // The default arrangement has every regular panel open
      bool visibilityChanged = false;
      for (auto& panel : m_Panels)
      {
        const EditorPanelDescriptor& descriptor = panel->GetDescriptor();
        if (descriptor.developer || panel->IsVisible() == descriptor.defaultVisible)
          continue;

        panel->SetVisible(descriptor.defaultVisible);
        StorePanelVisibility(*panel);
        visibilityChanged = true;
      }
      if (visibilityChanged)
        m_Preferences.Save();
      b_ResetLayout = true;
    }
  }

  void EditorLayer::DrawThemeMenu()
  {
    EditorThemePreset preset = m_Preferences.themePreset;
    bool picked = false;

    for (size_t i = 0; i < size_t(EditorThemePalette::Count); i++)
    {
      const auto palette = EditorThemePalette(i);
      if (ImGui::MenuItem(GetEditorThemePaletteName(palette), nullptr, preset.palette == palette))
      {
        preset.palette = palette;
        picked = true;
      }
    }

    ImGui::Separator();
    for (size_t i = 0; i < size_t(EditorThemeMode::Count); i++)
    {
      const auto mode = EditorThemeMode(i);
      if (ImGui::MenuItem(GetEditorThemeModeName(mode), nullptr, preset.mode == mode))
      {
        preset.mode = mode;
        picked = true;
      }
    }

    if (!picked)
      return;

    // Color overrides and unsaved Developer panel edits go; saved metrics and font sizes stay
    m_Preferences.themePreset = preset;
    ApplyEditorThemePreset(m_Preferences.theme, preset);
    m_Preferences.Save();
    EditorStyle::RequestTheme(m_Preferences.theme);
  }

  void EditorLayer::RenderUI()
  {
    ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    if (b_ResetLayout)
    {
      BuildDefaultLayout(dockspaceId);
      b_ResetLayout = false;
    }
    else if (!b_LayoutBuilt)
    {
      // An older default layout is replaced once, whatever docking this imgui.ini restored
      const bool outdated = s_IniLayoutVersion < EDITOR_LAYOUT_VERSION;
      auto* node = ImGui::DockBuilderGetNode(dockspaceId);
      if (outdated || node == nullptr || !node->IsSplitNode())
        BuildDefaultLayout(dockspaceId);

      if (outdated)
      {
        YA_LOG_INFO("Editor", "Default dock layout rebuilt: imgui.ini had layout version %u, the editor uses %u",
          s_IniLayoutVersion, EDITOR_LAYOUT_VERSION);
        s_IniLayoutVersion = EDITOR_LAYOUT_VERSION;
        ImGui::MarkIniSettingsDirty();
      }

      b_LayoutBuilt = true;
    }

    if (ImGui::IsKeyDown(ImGuiMod_Ctrl))
    {
      if (ImGui::IsKeyPressed(ImGuiKey_N, false))
        b_PendingNewScene = true;
      if (ImGui::IsKeyPressed(ImGuiKey_S, false))
      {
        if (ImGui::IsKeyDown(ImGuiMod_Shift))
          SaveSceneAs();
        else
          SaveScene();
      }
      if (ImGui::IsKeyPressed(ImGuiKey_O, false))
        OpenScene();
    }

    if (ImGui::BeginMainMenuBar())
    {
      if (ImGui::BeginMenu("File"))
      {
        if (ImGui::MenuItem("New Scene", "Ctrl+N"))
          b_PendingNewScene = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))
          OpenScene();
        if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
          SaveScene();
        if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
          SaveSceneAs();
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("View"))
      {
        DrawViewMenu();
        ImGui::EndMenu();
      }

      size_t agentClients = m_Bridge.GetClientCount();
      if (agentClients > 0)
      {
        char label[32];
        snprintf(label, sizeof(label), "AI Agent (%zu)", agentClients);
        const ImGuiStyle& style = ImGui::GetStyle();
        float width = ImGui::CalcTextSize(label).x + style.ItemSpacing.x * 2.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - width - style.WindowPadding.x);

        ImGui::PushStyleColor(ImGuiCol_Text, ToImGuiColor(EditorStyle::GetTheme().success));
        bool clicked = ImGui::MenuItem(label);
        ImGui::PopStyleColor();
        if (clicked)
        {
          SetPanelVisible(*m_AgentPanel, true);
          m_AgentPanel->RequestFocus();
        }
      }

      ImGui::EndMainMenuBar();
    }

    if (m_Context.ConsumeSelectionChanged() && m_Context.selectedEntity != entt::null)
    {
      // Opened when closed but never focused, so a selection does not switch the tab in front
      SetPanelVisible(*m_DetailsPanel, true);

      if (m_Context.scene->HasComponent<MaterialComponent>(m_Context.selectedEntity))
      {
        auto& mc = m_Context.scene->GetComponent<MaterialComponent>(m_Context.selectedEntity);
        m_Context.SelectMaterial(mc.asset);
      }
      else
      {
        m_Context.ClearMaterialSelection();
      }
    }

    // Show in Outliner has nothing to show the entity in while the outliner is closed
    if (m_Context.revealEntityRequest != entt::null)
      SetPanelVisible(*m_OutlinerPanel, true);

    for (auto& panel : m_Panels)
    {
      if (!panel->IsVisible())
        continue;

      panel->OnRender(m_Context);
      // The window's close button clears the visibility during OnRender
      if (!panel->IsVisible())
      {
        StorePanelVisibility(*panel);
        m_Preferences.Save();
      }
    }

    // A closed viewport no longer refreshes these, and stale values would keep viewport input live
    if (!m_ViewportPanel->IsVisible())
    {
      m_Context.viewportHovered = false;
      m_Context.mouseInViewportValid = false;
    }

    GetInput().SetViewportHovered(m_Context.viewportHovered);
  }

  namespace
  {
    // Below this an L0 channel carries no usable light, so a directional term on
    // top of it counts as full ringing instead of dividing by ~0
    constexpr float MIN_RINGING_L0 = 1e-4f;
    // Ratio at which the ringing ramp reaches its hottest color
    constexpr float MAX_RINGING_RATIO = 2.0f;

    // Brightest L0 channel over the valid nodes. A single scalar for the whole
    // volume is what keeps the per-node hue intact - normalizing each channel by
    // its own maximum would pull every bright node toward white.
    float FindPeakNodeL0(const IrradianceVolumeFileData& data)
    {
      float peak = 0.0f;
      size_t count = data.coefficients.size();
      for (size_t i = 0; i < count; i++)
      {
        if (data.validity[i] == 0)
          continue;

        const SHL1RGB sh = UnpackSHL1RGBHalf(data.coefficients[i]);
        peak = std::max({ peak, sh.r.l0, sh.g.l0, sh.b.l0 });
      }
      return peak;
    }

    // A node shared by bricks of different spacings is sized by the finest of them.
    void BuildVolumeNodeGizmos(const IrradianceVolumeFileData& data, std::vector<glm::vec4>& outNodes)
    {
      outNodes.assign(data.GetNodeCount(), glm::vec4(0.0f));
      for (size_t b = 0; b < data.bricks.size(); b++)
      {
        const IrradianceBrick& brick = data.bricks[b];
        const int32_t stepKeys = GetIrradianceBrickStepKeys(brick.spacingIndex);
        const float halfSize = std::max(0.08f * IRRADIANCE_SPACINGS[brick.spacingIndex], 0.01f);
        size_t local = b * IRRADIANCE_BRICK_NODE_COUNT;
        for (int32_t z = 0; z < int32_t(IRRADIANCE_BRICK_NODES); z++)
        {
          for (int32_t y = 0; y < int32_t(IRRADIANCE_BRICK_NODES); y++)
          {
            for (int32_t x = 0; x < int32_t(IRRADIANCE_BRICK_NODES); x++)
            {
              glm::vec4& node = outNodes[data.brickNodeIndices[local++]];
              const glm::vec3 position = GetIrradianceKeyWorldPosition(brick.originKey + glm::ivec3(x, y, z) * stepKeys);
              node = glm::vec4(position, node.w > 0.0f ? std::min(node.w, halfSize) : halfSize);
            }
          }
        }
      }
    }

    // irradiance(n) = l0 + dot(l1, n) reaches its minimum at l0 - |l1|, so the
    // reconstruction goes negative for some normals exactly when this exceeds 1
    float ChannelRingingRatio(const SHL1Channel& channel)
    {
      float l0 = std::max(channel.l0, 0.0f);
      float l1 = glm::length(glm::vec3(channel.l1x, channel.l1y, channel.l1z));
      if (l0 <= MIN_RINGING_L0)
        return l1 <= MIN_RINGING_L0 ? 0.0f : MAX_RINGING_RATIO;
      return l1 / l0;
    }

    // Worst channel wins: one channel clamping to zero already distorts the color
    float NodeRingingRatio(const SHL1RGB& sh)
    {
      return std::max({ ChannelRingingRatio(sh.r), ChannelRingingRatio(sh.g), ChannelRingingRatio(sh.b) });
    }

    // Green to yellow while the fit stays non-negative, then a deliberate break -
    // opaque red to white-hot - for the nodes the shader clamps to black
    glm::vec4 RingingNodeColor(float ratio)
    {
      if (ratio <= 1.0f)
      {
        glm::vec3 safe = glm::mix(glm::vec3(0.05f, 0.65f, 0.15f), glm::vec3(0.85f, 0.85f, 0.1f), ratio);
        return glm::vec4(safe, 0.3f);
      }

      float excess = glm::clamp((ratio - 1.0f) / (MAX_RINGING_RATIO - 1.0f), 0.0f, 1.0f);
      glm::vec3 ringing = glm::mix(glm::vec3(1.0f, 0.05f, 0.05f), glm::vec3(1.0f, 0.9f, 0.85f), excess);
      return glm::vec4(ringing, 1.0f);
    }
  }

  void EditorLayer::DebugDrawIrradianceVolumeBricks()
  {
    auto* render = m_Context.render;
    if (!render || !render->GetVolumeBricksVisible())
      return;

    Entity selected = m_Context.selectedEntity;
    if (selected == entt::null || !GetScene().HasComponent<IrradianceVolumeComponent>(selected))
      return;

    const IrradianceVolumePlacementPreview* preview = render->FindIrradianceVolumePlacementPreview(selected);
    if (preview == nullptr)
      return;

    const auto strides = Render::GetPreviewBrickDrawStrides(*preview, render->GetVolumeNodeGizmosDrawn());
    std::array<uint32_t, IRRADIANCE_SPACINGS.size()> seen {};
    auto& gizmo = render->GetGizmoRenderer();
    for (const IrradianceBrick& brick : preview->bricks)
    {
      const uint32_t level = std::min(brick.spacingIndex, uint32_t(IRRADIANCE_SPACINGS.size() - 1));
      if (seen[level]++ % strides[level] != 0)
        continue;

      glm::vec3 halfSize(0.5f * float(GetIrradianceBrickSizeKeys(brick.spacingIndex)) * IRRADIANCE_SPACINGS[0]);
      gizmo.DrawWireBoxDepthTested(GetIrradianceKeyWorldPosition(brick.originKey) + halfSize, halfSize,
        Render::GetPlacementBrickColor(brick.spacingIndex));
    }
  }

  void EditorLayer::ReleaseVolumeNodeCache()
  {
    m_VolumeNodeCachePath.clear();
    b_VolumeNodeCacheValid = false;
    m_VolumeNodeCache = {};
    std::vector<glm::vec4>().swap(m_VolumeNodeGizmos);
    m_VolumeNodePeakL0 = 0.0f;
  }

  void EditorLayer::DebugDrawIrradianceVolumeNodes()
  {
    // A whole scene volume and its node gizmos take hundreds of megabytes, so the cache is only
    // held while the overlay shows the selected volume.
    auto* render = m_Context.render;
    if (!render || !render->GetVolumeNodesVisible())
    {
      ReleaseVolumeNodeCache();
      return;
    }

    Entity selected = m_Context.selectedEntity;
    if (selected == entt::null || !GetScene().HasComponent<IrradianceVolumeComponent>(selected))
    {
      ReleaseVolumeNodeCache();
      return;
    }

    auto& volume = GetScene().GetComponent<IrradianceVolumeComponent>(selected);
    if (volume.bakedVolumePath.empty())
    {
      ReleaseVolumeNodeCache();
      return;
    }

    // A rebake writes to the same path, so the cache also follows the uploaded set: every bake,
    // reload and scene load replaces it.
    const uint32_t generation = render->GetIrradianceVolumeGeneration();
    if (m_VolumeNodeCachePath != volume.bakedVolumePath || m_VolumeNodeCacheGeneration != generation)
    {
      // First, so the old volume and the new one are never held at once.
      ReleaseVolumeNodeCache();
      m_VolumeNodeCachePath = volume.bakedVolumePath;
      m_VolumeNodeCacheGeneration = generation;
      std::string resolved = m_Context.assetManager->ResolvePath(volume.bakedVolumePath);
      b_VolumeNodeCacheValid = IrradianceVolumeFile::Load(resolved, m_VolumeNodeCache);
      m_VolumeNodePeakL0 = b_VolumeNodeCacheValid ? FindPeakNodeL0(m_VolumeNodeCache) : 0.0f;
      if (b_VolumeNodeCacheValid)
        BuildVolumeNodeGizmos(m_VolumeNodeCache, m_VolumeNodeGizmos);
      else
        m_VolumeNodeGizmos.clear();
    }

    if (!b_VolumeNodeCacheValid)
      return;

    const IrradianceVolumeFileData& data = m_VolumeNodeCache;
    uint32_t nodeCount = uint32_t(m_VolumeNodeGizmos.size());
    if (nodeCount == 0)
      return;

    // Every node is a wire box, so a dense grid is drawn sparsely instead of
    // sinking the frame rate
    constexpr uint32_t MAX_DRAWN_NODES = 20000;
    uint32_t stride = std::max(1u, (nodeCount + MAX_DRAWN_NODES - 1) / MAX_DRAWN_NODES);

    auto& gizmo = render->GetGizmoRenderer();
    bool showRejected = render->GetVolumeInvalidNodesVisible();
    VolumeNodeColorMode colorMode = render->GetVolumeNodeColorMode();

    // The bricks stored in the asset are used on purpose: a volume that moved or
    // was resized since it was baked then visibly disagrees with its bounds gizmo.
    uint32_t drawn = 0;
    for (uint32_t index = 0; index < nodeCount; index += stride)
    {
      bool valid = data.validity[index] != 0;
      if (!valid && !showRejected)
        continue;

      // A node no brick references has no position to draw at
      const glm::vec4& node = m_VolumeNodeGizmos[index];
      if (node.w <= 0.0f)
        continue;

      glm::vec4 color;
      if (!valid)
      {
        color = glm::vec4(1.0f, 0.0f, 1.0f, 0.35f);
      }
      else if (colorMode == VolumeNodeColorMode::Ringing)
      {
        color = RingingNodeColor(NodeRingingRatio(UnpackSHL1RGBHalf(data.coefficients[index])));
      }
      else
      {
        const SHL1RGB sh = UnpackSHL1RGBHalf(data.coefficients[index]);
        glm::vec3 l0 = glm::max(glm::vec3(sh.r.l0, sh.g.l0, sh.b.l0), glm::vec3(0.0f));
        // Relative to the peak of this volume, so the contrast fills the ramp
        // whatever the absolute scene brightness is. Gizmos are drawn after the
        // tonemap pass, so the gamma here is the display encode - and it is also
        // what lifts shadowed nodes out of a near-black cluster.
        glm::vec3 normalized = m_VolumeNodePeakL0 > 0.0f
          ? l0 / m_VolumeNodePeakL0
          : glm::vec3(0.0f);
        color = glm::vec4(glm::pow(normalized, glm::vec3(1.0f / 2.2f)), 0.9f);
      }

      gizmo.DrawWireBoxDepthTested(glm::vec3(node), glm::vec3(node.w), color);
      drawn++;
    }
    render->SetVolumeNodeGizmosDrawn(drawn);
  }

  // Light and probe icons are camera-facing quads drawn in the overlay pass with no depth
  // test, so picking has to reproduce that quad exactly - same size, same glyph aspect,
  // same camera basis as gizmo_sprite.vert - or the clickable area drifts off the pixels
  // that are actually on screen.
  Entity EditorLayer::PickIconEntity(const Ray& ray, const glm::mat4& view, const glm::mat4& proj)
  {
    if (!m_Context.render->GetGizmosEnabled())
      return entt::null;

    auto& gizmo = m_Context.render->GetGizmoRenderer();
    glm::vec3 right(view[0][0], view[1][0], view[2][0]);
    glm::vec3 up(view[0][1], view[1][1], view[2][1]);
    glm::vec3 normal = glm::cross(right, up);

    Entity closest = entt::null;
    float closestDist = std::numeric_limits<float>::max();

    auto testIcon = [&](Entity entity, const glm::vec3& center, uint32_t codepoint) {
      auto hit = RayPlaneIntersect(ray, center, normal);
      if (!hit || *hit >= closestDist)
        return;

      glm::vec3 offset = ray.origin + *hit * ray.direction - center;
      float size = GizmoRenderer::ClampSpriteSize(EditorIcon::WORLD_SIZE, center, view, proj,
        float(m_Context.viewportHeight), gizmo.GetSpriteMaxPixels());
      float halfWidth = size * 0.5f * gizmo.GetSpriteAspect(codepoint);
      float halfHeight = size * 0.5f;
      if (std::abs(glm::dot(offset, right)) > halfWidth ||
          std::abs(glm::dot(offset, up)) > halfHeight)
        return;

      closestDist = *hit;
      closest = entity;
    };

    // Mirrors how Render queues the light icons, buffer caps and all: a light that draws
    // no icon must not catch clicks either.
    int pointCount = 0;
    int spotCount = 0;
    bool hasDirectional = false;
    for (auto [entity, light, wt] : GetScene().GetView<LightComponent, WorldTransform>().each())
    {
      glm::vec3 position(wt.world[3]);
      switch (light.type)
      {
        case LightType::Point:
          if (pointCount++ < MAX_POINT_LIGHTS)
            testIcon(entity, position, EditorIcon::LIGHT_BULB);
          break;
        case LightType::Spot:
          if (spotCount++ < MAX_SPOT_LIGHTS)
            testIcon(entity, position, EditorIcon::LIGHT_BULB);
          break;
        case LightType::Directional:
          // Only the first directional light reaches the snapshot, and it draws an icon
          // only while it contributes anything
          if (!hasDirectional)
          {
            hasDirectional = true;
            if (light.intensity > 0.0f)
              testIcon(entity, position, EditorIcon::SUN);
          }
          break;
      }
    }

    for (auto [entity, probe, wt] : GetScene().GetView<ReflectionProbeComponent, WorldTransform>().each())
      testIcon(entity, glm::vec3(wt.world[3]), EditorIcon::PROBE);

    // Mirrors DebugDrawSceneCameras: the editor camera draws no icon, so it catches no
    // clicks either
    for (auto [entity, camera, wt] : GetScene().GetView<CameraComponent, WorldTransform>().each())
    {
      if (GetScene().HasComponent<EditorOnlyTag>(entity))
        continue;
      testIcon(entity, glm::vec3(wt.world[3]), EditorIcon::CAMERA);
    }

    return closest;
  }

  // A model imported as forty meshes is one object to work with, so a plain click selects
  // that object and Ctrl drills into the exact mesh the pixel belongs to.
  Entity EditorLayer::FindSelectionRoot(Entity entity)
  {
    Entity root = entity;
    while (GetScene().HasComponent<HierarchyComponent>(root))
    {
      Entity parent = GetScene().GetHierarchy(root).parent;
      if (parent == entt::null || !GetScene().GetRegistry().valid(parent))
        break;

      root = parent;
    }
    return root;
  }

  void EditorLayer::ApplyPickResult(const PickResult& result)
  {
    if (result.hit)
    {
      Entity entity = static_cast<Entity>(result.entityId);
      // The frames between the click and the answer are enough to delete an object
      if (GetScene().GetRegistry().valid(entity))
      {
        m_Context.SelectEntity(b_PickRequestExact ? entity : FindSelectionRoot(entity));
        return;
      }
    }

    // Nothing was rasterized there. Entities without geometry never appear in the id
    // buffer at all, so they get their chance here before the selection is dropped.
    Entity fallback = PickByRay(m_PickFallbackRay);
    if (fallback != entt::null)
      m_Context.SelectEntity(fallback);
    else
      m_Context.ClearSelection();
  }

  // Entities without geometry - cameras, empties, irradiance volumes - never show up in
  // the id buffer, so a small sphere around the pivot stays the only way to reach them.
  // Anything that does have bounds is geometry and belongs to the id pass, which is why
  // the AABB test that used to live here is gone: guessing from bounds is exactly what
  // made nested objects unpickable.
  Entity EditorLayer::PickByRay(const Ray& ray)
  {
    Entity closest = entt::null;
    float closestDist = std::numeric_limits<float>::max();

    for (auto [entity, wt] : GetScene().GetView<WorldTransform>().each())
    {
      if (GetScene().HasComponent<WorldBounds>(entity))
        continue;
      if (GetScene().HasComponent<EditorOnlyTag>(entity) || GetScene().HasComponent<HiddenTag>(entity))
        continue;

      auto hit = RaySphereIntersect(ray, glm::vec3(wt.world[3]), EditorIcon::WORLD_SIZE);
      if (hit && *hit < closestDist)
      {
        closestDist = *hit;
        closest = entity;
      }
    }

    return closest;
  }

  namespace
  {
    // Scene cameras normally see a kilometre; drawing that would fill the level with one
    // wireframe, so the visualization stops early. The near end stays exact.
    constexpr float CAMERA_FRUSTUM_VIS_FAR = 1.0f;
    const glm::vec4 kCameraIconColor(0.7f, 0.8f, 0.9f, 0.85f);
    const glm::vec4 kCameraFrustumColor(0.45f, 0.6f, 0.75f, 0.45f);
    const glm::vec4 kCameraFrustumSelectedColor(1.0f, 0.75f, 0.25f, 0.9f);
    const glm::vec4 kTrackPathColor(0.35f, 0.75f, 0.95f, 0.8f);
    const glm::vec4 kTrackKeyColor(0.45f, 0.6f, 0.75f, 0.7f);
    // Enough to show the Hermite curvature between two keys without flooding the instance
    // buffer on a long track
    constexpr uint32_t TRACK_SAMPLES_PER_SEGMENT = 16;
    constexpr float TRACK_KEY_RADIUS = 0.12f;
    // Slightly padded relative to the drawn sphere so the small targets are comfortable to hit
    constexpr float TRACK_KEY_PICK_RADIUS = 0.18f;

    const glm::vec4 kMotionPathColor(0.95f, 0.75f, 0.3f, 0.85f);
    const glm::vec4 kMotionPathTightColor(1.0f, 0.25f, 0.2f, 0.95f);
    const glm::vec4 kMotionPathTickColor(0.95f, 0.95f, 0.95f, 0.8f);
    const glm::vec4 kMotionPathPointColor(0.95f, 0.75f, 0.3f, 0.7f);
    // Table samples per drawn segment: 10 * 5 cm draws the curve every half metre
    constexpr size_t MOTION_PATH_DRAW_STRIDE = 10;
    // Lifted off the road the curve usually lies on, so the depth test does not eat it
    constexpr float MOTION_PATH_DRAW_LIFT = 0.05f;
    constexpr float MOTION_PATH_TICK_HEIGHT = 0.6f;
    constexpr int MOTION_PATH_MAX_TICKS = 600;
    constexpr float MOTION_PATH_POINT_RADIUS = 0.2f;
    constexpr float MOTION_PATH_POINT_PICK_RADIUS = 0.3f;

    // World matrix with the scale divided out: the frustum shape comes from fov and the
    // plane distances, so a scaled camera entity must not stretch it into something the
    // renderer would never produce.
    glm::mat4 CameraOrientationMatrix(const glm::mat4& world)
    {
      glm::mat4 result = world;
      for (int i = 0; i < 3; i++)
      {
        glm::vec3 axis(result[i]);
        float length = glm::length(axis);
        if (length > 1e-6f)
          result[i] = glm::vec4(axis / length, 0.0f);
      }
      return result;
    }
  }

  void EditorLayer::DebugDrawSceneCameras()
  {
    if (!m_Context.render)
      return;

    auto& gizmo = m_Context.render->GetGizmoRenderer();
    bool drawFrustums = m_Context.render->GetCameraFrustumsVisible();

    for (auto [entity, camera, wt] : GetScene().GetView<CameraComponent, WorldTransform>().each())
    {
      if (GetScene().HasComponent<EditorOnlyTag>(entity))
        continue;

      gizmo.DrawSprite(glm::vec3(wt.world[3]), EditorIcon::WORLD_SIZE, EditorIcon::CAMERA,
        kCameraIconColor);

      if (!drawFrustums)
        continue;
      // Previewing this camera puts the viewport inside its own frustum
      if (m_Context.previewCamera == entity)
        continue;

      float farDist = std::min(camera.farPlane, CAMERA_FRUSTUM_VIS_FAR);
      bool selected = m_Context.selectedEntity == entity;
      gizmo.DrawWireFrustum(CameraOrientationMatrix(wt.world), camera.fov, camera.aspectRatio,
        camera.nearPlane, farDist,
        selected ? kCameraFrustumSelectedColor : kCameraFrustumColor);
    }
  }

  void EditorLayer::DebugDrawCameraTrack()
  {
    if (!m_Context.render)
      return;

    auto hasTrack = [this](Entity e) {
      return e != entt::null && GetScene().GetRegistry().valid(e)
        && GetScene().HasComponent<CameraTrackComponent>(e);
    };

    // The sequencer's binding wins: it survives selecting something else, which is what
    // keeps the path on screen while the aim target or a light is being adjusted.
    Entity trackEntity = hasTrack(m_Context.sequencerTrack)
      ? m_Context.sequencerTrack
      : m_Context.selectedEntity;
    if (!hasTrack(trackEntity))
      return;

    // Looking through the track camera puts the whole path behind its near plane
    if (m_Context.previewCamera == trackEntity)
      return;

    auto& track = GetScene().GetComponent<CameraTrackComponent>(trackEntity);
    if (track.keys.empty())
      return;

    auto& gizmo = m_Context.render->GetGizmoRenderer();

    glm::vec3 previous = track.keys.front().position;
    for (size_t seg = 0; seg + 1 < track.keys.size(); seg++)
    {
      for (uint32_t sample = 1; sample <= TRACK_SAMPLES_PER_SEGMENT; sample++)
      {
        float t = glm::mix(track.keys[seg].time, track.keys[seg + 1].time,
          float(sample) / float(TRACK_SAMPLES_PER_SEGMENT));
        glm::vec3 point = EvaluateCameraTrack(track.keys, t).position;
        gizmo.DrawLine(previous, point, kTrackPathColor);
        previous = point;
      }
    }

    int selectedKey = trackEntity == m_Context.sequencerTrack ? m_Context.sequencerSelectedKey : -1;
    for (size_t i = 0; i < track.keys.size(); i++)
    {
      gizmo.DrawWireSphereDepthTested(track.keys[i].position, TRACK_KEY_RADIUS,
        int(i) == selectedKey ? kCameraFrustumSelectedColor : kTrackKeyColor);
    }
  }

  CameraTrackKey* EditorLayer::ActiveSequencerKey()
  {
    Entity track = m_Context.sequencerTrack;
    int index = m_Context.sequencerSelectedKey;
    // Only while the track entity itself is selected: any other selection keeps the
    // regular entity gizmo
    if (track == entt::null || index < 0 || m_Context.selectedEntity != track)
      return nullptr;
    if (!GetScene().GetRegistry().valid(track) || !GetScene().HasComponent<CameraTrackComponent>(track))
      return nullptr;

    auto& trackComponent = GetScene().GetComponent<CameraTrackComponent>(track);
    if (index >= int(trackComponent.keys.size()))
      return nullptr;
    return &trackComponent.keys[index];
  }

  void EditorLayer::DragSequencerKey(const glm::vec3& delta, const glm::vec3& currentHit)
  {
    CameraTrackKey* key = ActiveSequencerKey();
    if (key == nullptr)
      return;

    if (m_DragMode == GizmoMode::Translate)
    {
      // Keys live in world space, so no parent conversion is involved
      key->position = m_DragStartWorldPos + glm::dot(delta, m_DragAxisDir) * m_DragAxisDir;
    }
    else if (m_DragMode == GizmoMode::Rotate)
    {
      glm::vec3 v1 = m_DragStartHitPoint - m_DragStartWorldPos;
      glm::vec3 v2 = currentHit - m_DragStartWorldPos;
      float l1 = glm::length(v1);
      float l2 = glm::length(v2);
      if (l1 > 1e-6f && l2 > 1e-6f)
      {
        v1 /= l1;
        v2 /= l2;
        float cosAngle = glm::clamp(glm::dot(v1, v2), -1.0f, 1.0f);
        float sinAngle = glm::dot(glm::cross(v1, v2), m_DragAxisDir);
        float angle = std::atan2(sinAngle, cosAngle);
        key->rotation = glm::normalize(
          glm::angleAxis(angle, m_DragAxisDir) * m_DragStartLocalTransform.rotation);
      }
    }

    // The camera entity follows the dragged key, exactly like a scrub to its time; the
    // panel is asked to move its playhead there so the two stay in step
    m_Context.sequencePlayer->Scrub(*m_Context.scene, m_Context.sequencerTrack, key->time);
    m_Context.sequencerScrubRequest = float(m_Context.sequencePlayer->GetTime());
  }

  bool EditorLayer::PickTrackKey(const Ray& ray, Entity& outTrack, int& outKey)
  {
    if (!m_Context.render || !m_Context.render->GetGizmosEnabled())
      return false;

    // Clickable exactly when visible, so this mirrors DebugDrawCameraTrack's track
    // resolution and guards
    auto hasTrack = [this](Entity e) {
      return e != entt::null && GetScene().GetRegistry().valid(e)
        && GetScene().HasComponent<CameraTrackComponent>(e);
    };

    Entity trackEntity = hasTrack(m_Context.sequencerTrack)
      ? m_Context.sequencerTrack
      : m_Context.selectedEntity;
    if (!hasTrack(trackEntity) || m_Context.previewCamera == trackEntity)
      return false;

    auto& track = GetScene().GetComponent<CameraTrackComponent>(trackEntity);
    float closestDist = std::numeric_limits<float>::max();
    int closest = -1;
    for (size_t i = 0; i < track.keys.size(); i++)
    {
      auto hit = RaySphereIntersect(ray, track.keys[i].position, TRACK_KEY_PICK_RADIUS);
      if (hit && *hit < closestDist)
      {
        closestDist = *hit;
        closest = int(i);
      }
    }

    if (closest < 0)
      return false;

    outTrack = trackEntity;
    outKey = closest;
    return true;
  }

  Entity EditorLayer::VisibleMotionPath()
  {
    auto hasPath = [this](Entity e) {
      return e != entt::null && GetScene().GetRegistry().valid(e)
        && GetScene().HasComponent<MotionPathComponent>(e);
    };

    if (hasPath(m_Context.sequencerPath))
      return m_Context.sequencerPath;
    return hasPath(m_Context.selectedEntity) ? m_Context.selectedEntity : Entity(entt::null);
  }

  void EditorLayer::DebugDrawMotionPath()
  {
    if (!m_Context.render)
      return;

    Entity pathEntity = VisibleMotionPath();
    if (pathEntity == entt::null)
      return;

    const auto& path = GetScene().GetComponent<MotionPathComponent>(pathEntity);
    if (path.points.empty())
      return;

    auto& gizmo = m_Context.render->GetGizmoRenderer();
    const MotionPathTable& table = path.GetTable();
    const glm::vec3 lift(0.0f, MOTION_PATH_DRAW_LIFT, 0.0f);
    const float tightLimit = 1.0f / std::max(path.minTurnRadius, 0.01f);

    const size_t sampleCount = table.positions.size();
    for (size_t i = 0; i + 1 < sampleCount; i += MOTION_PATH_DRAW_STRIDE)
    {
      size_t j = std::min(i + MOTION_PATH_DRAW_STRIDE, sampleCount - 1);
      bool tight = std::abs(table.curvature[i]) > tightLimit || std::abs(table.curvature[j]) > tightLimit;
      gizmo.DrawLine(table.positions[i] + lift, table.positions[j] + lift,
        tight ? kMotionPathTightColor : kMotionPathColor);
    }

    // Where the entity is at every whole second of its drive, so the timing reads in space
    if (!path.speedKeys.empty() && path.points.size() >= 2)
    {
      float end = MotionPathDuration(table, path.speedKeys);
      int first = int(std::ceil(path.speedKeys.front().time));
      int last = std::min(int(std::floor(end)), first + MOTION_PATH_MAX_TICKS);
      for (int second = first; second <= last; second++)
      {
        glm::vec3 position = EvaluateMotionPath(table, path.speedKeys, float(second)).position + lift;
        gizmo.DrawLine(position, position + glm::vec3(0.0f, MOTION_PATH_TICK_HEIGHT, 0.0f), kMotionPathTickColor);
      }
    }

    int selectedPoint = pathEntity == m_Context.sequencerPath ? m_Context.sequencerSelectedPoint : -1;
    for (size_t i = 0; i < path.points.size(); i++)
    {
      gizmo.DrawWireSphereDepthTested(path.points[i], MOTION_PATH_POINT_RADIUS,
        int(i) == selectedPoint ? kCameraFrustumSelectedColor : kMotionPathPointColor);
    }
  }

  glm::vec3* EditorLayer::ActivePathPoint()
  {
    Entity pathEntity = m_Context.sequencerPath;
    int index = m_Context.sequencerSelectedPoint;
    // Only while the path entity itself is selected: any other selection keeps the regular
    // entity gizmo
    if (pathEntity == entt::null || index < 0 || m_Context.selectedEntity != pathEntity)
      return nullptr;
    if (!GetScene().GetRegistry().valid(pathEntity) || !GetScene().HasComponent<MotionPathComponent>(pathEntity))
      return nullptr;

    auto& path = GetScene().GetComponent<MotionPathComponent>(pathEntity);
    if (index >= int(path.points.size()))
      return nullptr;
    return &path.points[index];
  }

  void EditorLayer::DragPathPoint(const glm::vec3& delta)
  {
    glm::vec3* point = ActivePathPoint();
    if (point == nullptr || m_DragMode != GizmoMode::Translate)
      return;

    // Points live in world space, so no parent conversion is involved
    *point = m_DragStartWorldPos + glm::dot(delta, m_DragAxisDir) * m_DragAxisDir;

    // A running session re-poses right away; a stopped one is not started by a drag
    SequencePlayer* player = m_Context.sequencePlayer;
    if (player != nullptr && player->IsActive())
      player->Scrub(*m_Context.scene, player->GetCameraTrack(), player->GetTime());
  }

  bool EditorLayer::PickPathPoint(const Ray& ray, Entity& outPath, int& outPoint)
  {
    if (!m_Context.render || !m_Context.render->GetGizmosEnabled())
      return false;

    // Clickable exactly when visible
    Entity pathEntity = VisibleMotionPath();
    if (pathEntity == entt::null)
      return false;

    const auto& path = GetScene().GetComponent<MotionPathComponent>(pathEntity);
    float closestDist = std::numeric_limits<float>::max();
    int closest = -1;
    for (size_t i = 0; i < path.points.size(); i++)
    {
      auto hit = RaySphereIntersect(ray, path.points[i], MOTION_PATH_POINT_PICK_RADIUS);
      if (hit && *hit < closestDist)
      {
        closestDist = *hit;
        closest = int(i);
      }
    }

    if (closest < 0)
      return false;

    outPath = pathEntity;
    outPoint = closest;
    return true;
  }

  void EditorLayer::DebugDrawGizmos()
  {
    DebugDrawIrradianceVolumeNodes();
    DebugDrawIrradianceVolumeBricks();

    // Reflection probe icons. Queued from the scene rather than from the snapshot the
    // renderer draws the influence volumes from: that snapshot skips unbaked probes, and
    // with the volumes hidden such a probe would have nothing on screen at all.
    if (m_Context.render)
    {
      auto& probeGizmo = m_Context.render->GetGizmoRenderer();
      for (auto [entity, probe, wt] : GetScene().GetView<ReflectionProbeComponent, WorldTransform>().each())
        probeGizmo.DrawSprite(glm::vec3(wt.world[3]), EditorIcon::WORLD_SIZE, EditorIcon::PROBE,
          glm::vec4(0.2f, 0.7f, 0.9f, 0.85f));
    }

    DebugDrawSceneCameras();
    DebugDrawCameraTrack();
    DebugDrawMotionPath();

    if (!m_Context.render || !m_Context.render->GetCollidersVisible())
      return;

    auto& gizmo = m_Context.render->GetGizmoRenderer();

    constexpr uint32_t MAX_INSTANCED_AABBS = 2000;
    static bool s_InstancedOverflowReported = false;
    const glm::vec4 staticColor(0.2f, 0.9f, 0.3f, 0.85f);

    for (auto [entity, collider, wt] : GetScene().GetView<ColliderComponent, WorldTransform>().each())
    {
      glm::vec3 center = glm::vec3(wt.world * glm::vec4(collider.localOffset, 1.0f));
      glm::vec3 scale(
        glm::length(glm::vec3(wt.world[0])),
        glm::length(glm::vec3(wt.world[1])),
        glm::length(glm::vec3(wt.world[2])));
      glm::mat3 rotMat(
        glm::vec3(wt.world[0]) / (scale.x > 0.0f ? scale.x : 1.0f),
        glm::vec3(wt.world[1]) / (scale.y > 0.0f ? scale.y : 1.0f),
        glm::vec3(wt.world[2]) / (scale.z > 0.0f ? scale.z : 1.0f));
      glm::quat rotation = glm::quat_cast(rotMat);
      gizmo.DrawWireBoxDepthTested(center, collider.halfExtents * scale, rotation, staticColor);
    }

    uint32_t instancedTotal = 0;
    for (auto [entity, instanced] : GetScene().GetView<InstancedColliderComponent>().each())
      instancedTotal += static_cast<uint32_t>(instanced.instances.size());

    uint32_t instancedDrawn = 0;
    for (auto [entity, instanced] : GetScene().GetView<InstancedColliderComponent>().each())
    {
      for (auto& entry : instanced.instances)
      {
        if (instancedDrawn >= MAX_INSTANCED_AABBS) break;
        gizmo.DrawWireBoxDepthTested(entry.center, entry.halfExtents, staticColor);
        instancedDrawn++;
      }
      if (instancedDrawn >= MAX_INSTANCED_AABBS) break;
    }

    if (instancedTotal > MAX_INSTANCED_AABBS && !s_InstancedOverflowReported)
    {
      YA_LOG_WARN("Physics", "Collider debug draw: %u instanced AABBs exceeds cap %u, truncating",
                  instancedTotal, MAX_INSTANCED_AABBS);
      s_InstancedOverflowReported = true;
    }
  }

  void EditorLayer::OnDetach()
  {
    m_Bridge.Shutdown();
    GetRender().WaitIdle();
    m_ViewportPanel->FlushPreferences();
    m_Panels.clear();
    EditorWidgets::SetPreferences(nullptr);
    m_TextureCache.Destroy();
    FileDialog::Shutdown();
  }

  static constexpr nfdu8filteritem_t s_SceneFilters[] = { { "Scene", "scene" } };

  static std::string FindProjectRoot(const std::filesystem::path& startDir)
  {
    auto dir = std::filesystem::weakly_canonical(startDir);
    while (dir.has_parent_path() && dir != dir.parent_path())
    {
      if (std::filesystem::exists(dir / "Assets"))
        return dir.string();
      dir = dir.parent_path();
    }
    return startDir.string();
  }

  void EditorLayer::EnsureBasePath(const std::string& scenePath)
  {
    if (!GetAssets().GetBasePath().empty())
      return;
    auto sceneDir = std::filesystem::path(scenePath).parent_path();
    GetAssets().SetBasePath(FindProjectRoot(sceneDir));
  }

  void EditorLayer::NewScene()
  {
    m_Registry->Get<SequencePlayer>().Stop(GetScene());
    m_Context.StopCameraPreview();
    m_Context.ClearSelection();
    m_Context.ClearMaterialSelection();

    GetRender().WaitIdle();
    m_TextureCache.Destroy();
    m_Registry->Get<SystemScheduler>().NotifySceneClear();
    GetAssets().DestroyAll();
    GetScene().ClearScene();
    GetRender().ClearIrradianceVolumePlacementPreviews();
    GetRender().ResetBoundState();

    GetAssets().Init(GetScene(), [this](uint32_t size) { return GetRender().AllocateInstanceData(size); });
    GetAssets().SetRenderContext(GetRender().GetContext(), GetRender().GetNoneTexture(), GetRender().GetCubicResources());

    auto* editorCam = GetLayerManager().GetLayer<EditorCameraLayer>();
    if (editorCam)
      editorCam->OnSceneReady();

    m_LastViewportWidth = 0;
    m_LastViewportHeight = 0;

    m_CurrentScenePath.clear();
    m_Bridge.SetScenePath(m_CurrentScenePath);

    // Panels keep scene state (expanded rows, searches, the bound track) that the old entities invalidated
    for (auto& panel : m_Panels)
      panel->OnSceneReady(m_Context);
  }

  void EditorLayer::SyncEditorCameraState()
  {
    auto* editorCam = GetLayerManager().GetLayer<EditorCameraLayer>();
    if (editorCam)
    {
      auto& camState = GetScene().GetEditorCameraState();
      camState.position = editorCam->GetPosition();
      camState.yaw = editorCam->GetYaw();
      camState.pitch = editorCam->GetPitch();
    }
  }

  void EditorLayer::SaveScene()
  {
    if (m_CurrentScenePath.empty())
    {
      SaveSceneAs();
      return;
    }
    SaveSceneTo(m_CurrentScenePath);
  }

  void EditorLayer::SaveSceneAs()
  {
    auto path = FileDialog::SaveFile(s_SceneFilters, 1, "scene.scene");
    if (path.empty())
      return;
    SaveSceneTo(path);
  }

  bool EditorLayer::SaveSceneTo(const std::string& path)
  {
    EnsureBasePath(path);
    SyncEditorCameraState();
    // A timeline session has entities posed away from where they belong; the file gets the originals
    m_Registry->Get<SequencePlayer>().Stop(GetScene());
    if (!SceneSerializer::Save(path, GetScene(), GetAssets(), *m_Context.componentRegistry, GetRender()))
      return false;

    // Only now: after a failed write the next plain Save still has to target the path that works
    m_CurrentScenePath = path;
    m_Bridge.SetScenePath(m_CurrentScenePath);
    return true;
  }

  void EditorLayer::OpenScene()
  {
    auto path = FileDialog::OpenFile(s_SceneFilters, 1);
    if (path.empty())
      return;

    m_PendingScenePath = path;
  }

  void EditorLayer::LoadSceneDeferred(const std::string& path)
  {
    m_Registry->Get<SequencePlayer>().Stop(GetScene());
    m_Context.StopCameraPreview();
    m_Context.ClearSelection();
    m_Context.ClearMaterialSelection();

    GetRender().WaitIdle();
    m_TextureCache.Destroy();
    m_Registry->Get<SystemScheduler>().NotifySceneClear();
    GetAssets().DestroyAll();
    GetScene().ClearScene();
    GetRender().ClearIrradianceVolumePlacementPreviews();
    GetRender().ResetBoundState();

    GetAssets().Init(GetScene(), [this](uint32_t size) { return GetRender().AllocateInstanceData(size); });
    GetAssets().SetRenderContext(GetRender().GetContext(), GetRender().GetNoneTexture(), GetRender().GetCubicResources());

    auto sceneDir = std::filesystem::path(path).parent_path();
    auto basePath = FindProjectRoot(sceneDir);

    auto& threadPool = m_Registry->Get<ThreadPool>();
    SceneSerializer::Load(path, GetScene(), GetAssets(),
      *m_Context.componentRegistry, GetRender(), basePath, &threadPool);

    // Recreate editor camera (destroyed by ClearScene)
    auto* editorCam = GetLayerManager().GetLayer<EditorCameraLayer>();
    if (editorCam)
      editorCam->OnSceneReady();

    // Force camera resize on next frame
    m_LastViewportWidth = 0;
    m_LastViewportHeight = 0;

    m_CurrentScenePath = path;
    m_Bridge.SetScenePath(m_CurrentScenePath);

    for (auto& panel : m_Panels)
      panel->OnSceneReady(m_Context);
  }

  void EditorLayer::BuildDefaultLayout(ImGuiID dockspaceId)
  {
    // Windows of retired panels that older imgui.ini files still carry
    static constexpr const char* RETIRED_WINDOWS[] = {
      "Console", "Content Browser", "Debug Viz", "Probe Preview", "Reflection Probe Preview"
    };
    for (const char* name : RETIRED_WINDOWS)
      ImGui::ClearWindowSettings(name);

    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

    // Shares of the whole work area
    constexpr float LEFT_COLUMN = 0.18f;
    constexpr float RIGHT_COLUMN = 0.26f;

    ImGuiID dockLeft;
    ImGuiID dockRemaining;
    ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, LEFT_COLUMN, &dockLeft, &dockRemaining);

    ImGuiID dockRight;
    ImGuiID dockCenter;
    ImGui::DockBuilderSplitNode(dockRemaining, ImGuiDir_Right, RIGHT_COLUMN / (1.0f - LEFT_COLUMN), &dockRight, &dockCenter);

    ImGuiID dockBottom;
    ImGuiID dockViewport;
    ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Down, 0.25f, &dockBottom, &dockViewport);

    ImGuiID dockLeftBottom;
    ImGuiID dockLeftTop;
    ImGui::DockBuilderSplitNode(dockLeft, ImGuiDir_Down, 0.4f, &dockLeftBottom, &dockLeftTop);

    ImGuiID dockRightBottom;
    ImGuiID dockRightTop;
    ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Down, 0.45f, &dockRightBottom, &dockRightTop);

    auto dock = [](const EditorPanelDescriptor& panel, ImGuiID node) { ImGui::DockBuilderDockWindow(panel.name, node); };
    dock(ViewportPanel::DESCRIPTOR, dockViewport);
    dock(OutlinerPanel::DESCRIPTOR, dockLeftTop);
    dock(MaterialBrowserPanel::DESCRIPTOR, dockLeftBottom);
    dock(DetailsPanel::DESCRIPTOR, dockRightTop);
    dock(MaterialInspectorPanel::DESCRIPTOR, dockRightTop);
    dock(RenderSettingsPanel::DESCRIPTOR, dockRightBottom);
    dock(DeveloperPanel::DESCRIPTOR, dockRightBottom);
    dock(SequencerPanel::DESCRIPTOR, dockBottom);
    dock(PerformancePanel::DESCRIPTOR, dockBottom);
    dock(AgentPanel::DESCRIPTOR, dockBottom);

    // A fresh node would put its last added tab in front. The id is the one ImGuiWindow gives its
    // tab: "#TAB" hashed in the window's own id scope.
    auto selectTab = [](ImGuiID nodeId, const EditorPanelDescriptor& panel)
    {
      if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(nodeId))
        node->SelectedTabId = ImHashStr("#TAB", 0, ImHashStr(panel.name));
    };
    selectTab(dockRightTop, DetailsPanel::DESCRIPTOR);
    selectTab(dockRightBottom, RenderSettingsPanel::DESCRIPTOR);
    selectTab(dockBottom, SequencerPanel::DESCRIPTOR);

    ImGui::DockBuilderFinish(dockspaceId);
  }
}
