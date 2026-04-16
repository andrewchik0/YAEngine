#include "Editor/EditorLayer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include "Input/InputSystem.h"
#include "LayerManager.h"
#include "Editor/Utils/EditorStyle.h"
#include "Editor/Panels/ViewportPanel.h"
#include "Editor/Panels/PerformancePanel.h"
#include "Editor/Panels/RenderSettingsPanel.h"
#include "Editor/Panels/OutlinerPanel.h"
#include "Editor/Panels/DetailsPanel.h"
#include "Editor/Panels/MaterialBrowserPanel.h"
#include "Editor/Panels/MaterialInspectorPanel.h"
#include "Editor/EditorCameraLayer.h"
#include "Editor/Utils/FileDialog.h"

#include "Assets/AssetManager.h"
#include "Render/Render.h"
#include "Scene/SceneSerializer.h"
#include "Scene/ComponentRegistry.h"
#include "Utils/ServiceRegistry.h"
#include "Utils/ThreadPool.h"
#include "Utils/Ray.h"
#include "Scene/SystemScheduler.h"

#include <glm/gtc/matrix_transform.hpp>

namespace YAEngine
{
  void EditorLayer::OnAttach()
  {
    FileDialog::Init();
    EditorStyle::Apply();
    GetLayerManager().PushLayer<EditorCameraLayer>();
    m_Panels.push_back(std::make_unique<ViewportPanel>());
    m_Panels.push_back(std::make_unique<OutlinerPanel>());
    m_Panels.push_back(std::make_unique<DetailsPanel>());
    m_Panels.push_back(std::make_unique<RenderSettingsPanel>());
    m_Panels.push_back(std::make_unique<PerformancePanel>());
    m_Panels.push_back(std::make_unique<MaterialBrowserPanel>());
    m_Panels.push_back(std::make_unique<MaterialInspectorPanel>());
  }

  void EditorLayer::OnSceneReady()
  {
    m_Context.scene = &GetScene();
    m_Context.assetManager = &GetAssets();
    m_Context.render = &GetRender();
    m_Context.timer = &GetTimer();
    m_Context.componentRegistry = &m_Registry->Get<ComponentRegistry>();

    m_TextureCache.Init(m_Context.assetManager);
    m_Context.textureCache = &m_TextureCache;

    if (m_CurrentScenePath.empty())
      m_CurrentScenePath = GetScene().GetScenePath();

    for (auto& panel : m_Panels)
      panel->OnSceneReady(m_Context);
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
    auto& gizmo = m_Context.render->GetGizmoRenderer();

    // Gizmo mode switching (1/2/3) - blocked during drag
    if (m_Context.viewportHovered && !b_DragActive)
    {
      if (input.IsKeyPressed(Key::D1)) m_Context.render->GetGizmoMode() = GizmoMode::Translate;
      if (input.IsKeyPressed(Key::D2)) m_Context.render->GetGizmoMode() = GizmoMode::Rotate;
      if (input.IsKeyPressed(Key::D3)) m_Context.render->GetGizmoMode() = GizmoMode::Scale;
    }

    // Build viewport ray
    Ray viewportRay {};
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
        glm::mat4 proj = glm::perspective(cam.fov, cam.aspectRatio, cam.nearPlane, cam.farPlane);

        viewportRay = ScreenToRay(m_Context.mouseInViewport, glm::inverse(proj), glm::inverse(view));
        hasViewportRay = true;
      }
    }

    // --- Drag state machine ---
    if (b_DragActive)
    {
      if (input.IsMouseReleased(MouseButton::Left))
      {
        // End drag
        b_DragActive = false;
        gizmo.SetDraggedAxis(GizmoAxis::None);
        input.SetGizmoDragging(false);
      }
      else if (hasViewportRay)
      {
        // Continue drag
        auto hitT = RayPlaneIntersect(viewportRay, m_DragStartWorldPos, m_DragPlaneNormal);
        if (hitT)
        {
          glm::vec3 currentHit = viewportRay.origin + *hitT * viewportRay.direction;
          glm::vec3 delta = currentHit - m_DragStartHitPoint;
          Entity entity = m_Context.selectedEntity;
          auto& scene = *m_Context.scene;
          auto& localTransform = scene.GetTransform(entity);

          // Get parent inverse world transform for hierarchy support
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
    else
    {
      // Hover detection (only when not dragging)
      if (hasViewportRay && m_Context.selectedEntity != entt::null && m_Context.viewportHovered)
        gizmo.UpdateHover(viewportRay);
      else
        gizmo.ClearHover();

      // LMB: begin drag or pick entity
      if (input.IsMousePressed(MouseButton::Left) && hasViewportRay)
      {
        GizmoAxis hoveredAxis = gizmo.GetHoveredAxis();

        if (hoveredAxis != GizmoAxis::None && m_Context.selectedEntity != entt::null)
        {
          // Begin drag
          Entity entity = m_Context.selectedEntity;
          auto& scene = *m_Context.scene;
          auto& wt = scene.GetComponent<WorldTransform>(entity);
          glm::vec3 gizmoPos(wt.world[3]);

          m_DragAxis = hoveredAxis;
          m_DragMode = m_Context.render->GetGizmoMode();
          m_DragStartLocalTransform = scene.GetTransform(entity);
          m_DragStartWorldPos = gizmoPos;
          m_DragAxisDir = AxisToDirection(hoveredAxis);

          // Compute drag plane
          Entity camEntity = GetScene().GetActiveCamera();
          glm::vec3 camPos = GetScene().GetTransform(camEntity).position;

          if (m_DragMode == GizmoMode::Rotate)
          {
            // Rotation ring plane: normal = axis direction
            m_DragPlaneNormal = m_DragAxisDir;
          }
          else
          {
            // Translate/Scale: plane containing axis, facing camera
            glm::vec3 camDir = glm::normalize(camPos - gizmoPos);
            m_DragPlaneNormal = ComputeDragPlaneNormal(m_DragAxisDir, camDir);
          }

          // Store gizmo scale for scale mode normalization
          float dist = glm::length(camPos - gizmoPos);
          m_DragGizmoScale = dist * 0.15f;

          // Intersect start ray with drag plane
          auto hitT = RayPlaneIntersect(viewportRay, m_DragStartWorldPos, m_DragPlaneNormal);
          if (hitT)
          {
            m_DragStartHitPoint = viewportRay.origin + *hitT * viewportRay.direction;
            b_DragActive = true;
            gizmo.SetDraggedAxis(hoveredAxis);
            input.SetGizmoDragging(true);
          }
        }
        else
        {
          // Entity picking
          Entity closest = entt::null;
          float closestDist = std::numeric_limits<float>::max();

          for (auto [entity, wt] : GetScene().GetView<WorldTransform>().each())
          {
            if (GetScene().HasComponent<EditorOnlyTag>(entity))
              continue;

            std::optional<float> hit;

            if (GetScene().HasComponent<WorldBounds>(entity))
            {
              auto& bounds = GetScene().GetComponent<WorldBounds>(entity);
              if (bounds.min != bounds.max)
                hit = RayAABBIntersect(viewportRay, bounds.min, bounds.max);
            }

            if (!hit)
            {
              glm::vec3 center(wt.world[3]);
              hit = RaySphereIntersect(viewportRay, center, 0.5f);
            }

            if (hit && *hit < closestDist)
            {
              closestDist = *hit;
              closest = entity;
            }
          }

          if (closest != entt::null)
            m_Context.SelectEntity(closest);
          else
            m_Context.ClearSelection();
        }
      }
    }

    // Update selected entity position for gizmo rendering
    if (m_Context.selectedEntity != entt::null && m_Context.scene->HasComponent<WorldTransform>(m_Context.selectedEntity))
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
      auto* node = ImGui::DockBuilderGetNode(dockspaceId);
      if (node == nullptr || !node->IsSplitNode())
        BuildDefaultLayout(dockspaceId);

      b_LayoutBuilt = true;
    }

    // Keyboard shortcuts
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
        for (auto& panel : m_Panels)
        {
          bool visible = panel->IsVisible();
          if (ImGui::MenuItem(panel->GetName(), nullptr, &visible))
            panel->SetVisible(visible);
        }

        if (m_Panels.size() > 0)
          ImGui::Separator();

        if (ImGui::MenuItem("Reset Layout"))
        {
          b_ResetLayout = true;
        }

        ImGui::EndMenu();
      }

      ImGui::EndMainMenuBar();
    }

    // Auto-open Details and auto-select material on entity selection
    if (m_Context.ConsumeSelectionChanged() && m_Context.selectedEntity != entt::null)
    {
      for (auto& panel : m_Panels)
      {
        if (std::strcmp(panel->GetName(), "Details") == 0)
          panel->SetVisible(true);
      }
      ImGui::SetWindowFocus("Details");

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

    for (auto& panel : m_Panels)
    {
      if (panel->IsVisible())
        panel->OnRender(m_Context);
    }

    GetInput().SetViewportHovered(m_Context.viewportHovered);
  }

  void EditorLayer::DebugDrawGizmos()
  {
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
      gizmo.DrawWireBoxDepthTested(center, collider.halfExtents * scale, staticColor);
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
    GetRender().WaitIdle();
    m_Panels.clear();
    m_TextureCache.Destroy();
    FileDialog::Shutdown();
  }

  static constexpr nfdu8filteritem_t s_SceneFilters[] = { { "Scene", "scene" } };

  // Walk up from startDir until we find a directory containing "Assets/"
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
    m_Context.ClearSelection();
    m_Context.ClearMaterialSelection();

    GetRender().WaitIdle();
    m_TextureCache.Destroy();
    m_Registry->Get<SystemScheduler>().NotifySceneClear();
    GetAssets().DestroyAll();
    GetScene().ClearScene();
    GetRender().ResetBoundState();

    GetAssets().Init(GetScene(), [this](uint32_t size) { return GetRender().AllocateInstanceData(size); });
    GetAssets().SetRenderContext(GetRender().GetContext(), GetRender().GetNoneTexture(), GetRender().GetCubicResources());

    auto* editorCam = GetLayerManager().GetLayer<EditorCameraLayer>();
    if (editorCam)
      editorCam->OnSceneReady();

    m_LastViewportWidth = 0;
    m_LastViewportHeight = 0;

    m_CurrentScenePath.clear();
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
    EnsureBasePath(m_CurrentScenePath);
    SyncEditorCameraState();
    SceneSerializer::Save(m_CurrentScenePath, GetScene(), GetAssets(),
      *m_Context.componentRegistry, GetRender());
  }

  void EditorLayer::SaveSceneAs()
  {
    auto path = FileDialog::SaveFile(s_SceneFilters, 1, "scene.scene");
    if (path.empty())
      return;
    m_CurrentScenePath = path;
    EnsureBasePath(m_CurrentScenePath);
    SyncEditorCameraState();
    SceneSerializer::Save(m_CurrentScenePath, GetScene(), GetAssets(),
      *m_Context.componentRegistry, GetRender());
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
    m_Context.ClearSelection();
    m_Context.ClearMaterialSelection();

    GetRender().WaitIdle();
    m_TextureCache.Destroy();
    m_Registry->Get<SystemScheduler>().NotifySceneClear();
    GetAssets().DestroyAll();
    GetScene().ClearScene();
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
  }

  void EditorLayer::BuildDefaultLayout(ImGuiID dockspaceId)
  {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

    ImGuiID dockLeft;
    ImGuiID dockRemaining;
    ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.18f, &dockLeft, &dockRemaining);

    ImGuiID dockBottom;
    ImGuiID dockCenterRight;
    ImGui::DockBuilderSplitNode(dockRemaining, ImGuiDir_Down, 0.25f, &dockBottom, &dockCenterRight);

    ImGuiID dockCenter;
    ImGuiID dockRight;
    ImGui::DockBuilderSplitNode(dockCenterRight, ImGuiDir_Right, 0.25f, &dockRight, &dockCenter);

    ImGuiID dockRightTop;
    ImGuiID dockRightBottom;
    ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Down, 0.45f, &dockRightBottom, &dockRightTop);

    ImGui::DockBuilderDockWindow("Outliner", dockLeft);
    ImGui::DockBuilderDockWindow("Viewport", dockCenter);
    ImGui::DockBuilderDockWindow("Details", dockRightTop);
    ImGui::DockBuilderDockWindow("Render Settings", dockRightTop);
    ImGui::DockBuilderDockWindow("Materials", dockRightTop);
    ImGui::DockBuilderDockWindow("Debug Viz", dockRightTop);
    ImGui::DockBuilderDockWindow("Material Inspector", dockRightBottom);
    ImGui::DockBuilderDockWindow("Performance", dockBottom);
    ImGui::DockBuilderDockWindow("Console", dockBottom);
    ImGui::DockBuilderDockWindow("Content Browser", dockBottom);

    ImGui::DockBuilderFinish(dockspaceId);
  }
}
