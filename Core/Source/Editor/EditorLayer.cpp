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
#include "Utils/Projection.h"
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

    // While the cursor is captured its reported position runs off into the distance, and
    // ImGui would light up panels under a pointer that is not there. The GLFW backend
    // leaves the cursor itself alone in this mode, so only the position needs suppressing.
    ImGuiIO& io = ImGui::GetIO();
    if (input.IsMouseCaptured())
      io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    else
      io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;

    auto& gizmo = m_Context.render->GetGizmoRenderer();

    // Gizmo mode switching (1/2/3) - blocked during drag
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

    // Build viewport ray
    Ray viewportRay {};
    glm::mat4 viewportView { 1.0f };
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
          // Icons first - they are drawn as an unoccluded overlay and never reach the id
          // buffer, so anything else winning over them would make a visible icon
          // unclickable. An icon always selects its own entity, with no root walk.
          Entity icon = PickIconEntity(viewportRay, viewportView);
          if (icon != entt::null)
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

        const SHL1RGB& sh = data.coefficients[i];
        peak = std::max({ peak, sh.r.l0, sh.g.l0, sh.b.l0 });
      }
      return peak;
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

  void EditorLayer::DebugDrawIrradianceVolumeNodes()
  {
    auto* render = m_Context.render;
    if (!render || !render->GetVolumeNodesVisible())
      return;

    Entity selected = m_Context.selectedEntity;
    if (selected == entt::null || !GetScene().HasComponent<IrradianceVolumeComponent>(selected))
      return;

    auto& volume = GetScene().GetComponent<IrradianceVolumeComponent>(selected);
    if (volume.bakedVolumePath.empty())
    {
      m_VolumeNodeCachePath.clear();
      b_VolumeNodeCacheValid = false;
      m_VolumeNodePeakL0 = 0.0f;
      return;
    }

    if (m_VolumeNodeCachePath != volume.bakedVolumePath)
    {
      m_VolumeNodeCachePath = volume.bakedVolumePath;
      std::string resolved = m_Context.assetManager->ResolvePath(volume.bakedVolumePath);
      b_VolumeNodeCacheValid = IrradianceVolumeFile::Load(resolved, m_VolumeNodeCache);
      m_VolumeNodePeakL0 = b_VolumeNodeCacheValid ? FindPeakNodeL0(m_VolumeNodeCache) : 0.0f;
    }

    if (!b_VolumeNodeCacheValid)
      return;

    const IrradianceVolumeFileData& data = m_VolumeNodeCache;
    uint32_t nodeCount = uint32_t(data.coefficients.size());
    if (nodeCount == 0 || data.nodesX == 0 || data.nodesY == 0)
      return;

    // Every node is a wire box, so a dense grid is drawn sparsely instead of
    // sinking the frame rate
    constexpr uint32_t MAX_DRAWN_NODES = 20000;
    uint32_t stride = std::max(1u, (nodeCount + MAX_DRAWN_NODES - 1) / MAX_DRAWN_NODES);

    auto& gizmo = render->GetGizmoRenderer();
    bool showRejected = render->GetVolumeInvalidNodesVisible();
    VolumeNodeColorMode colorMode = render->GetVolumeNodeColorMode();

    glm::vec3 nodeHalfExtents(std::max(0.08f * data.spacing, 0.01f));

    // The lattice stored in the asset is used on purpose: a volume that moved or
    // was resized since it was baked then visibly disagrees with its bounds gizmo.
    for (uint32_t index = 0; index < nodeCount; index += stride)
    {
      bool valid = data.validity[index] != 0;
      if (!valid && !showRejected)
        continue;

      uint32_t x = index % data.nodesX;
      uint32_t y = (index / data.nodesX) % data.nodesY;
      uint32_t z = index / (data.nodesX * data.nodesY);

      glm::vec3 world = data.latticeOrigin
        + glm::vec3(float(x), float(y), float(z)) * data.spacing;

      glm::vec4 color;
      if (!valid)
      {
        color = glm::vec4(1.0f, 0.0f, 1.0f, 0.35f);
      }
      else if (colorMode == VolumeNodeColorMode::Ringing)
      {
        color = RingingNodeColor(NodeRingingRatio(data.coefficients[index]));
      }
      else
      {
        const SHL1RGB& sh = data.coefficients[index];
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

      gizmo.DrawWireBoxDepthTested(world, nodeHalfExtents, color);
    }
  }

  // Light and probe icons are camera-facing quads drawn in the overlay pass with no depth
  // test, so picking has to reproduce that quad exactly - same size, same glyph aspect,
  // same camera basis as gizmo_sprite.vert - or the clickable area drifts off the pixels
  // that are actually on screen.
  Entity EditorLayer::PickIconEntity(const Ray& ray, const glm::mat4& view)
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
      float halfWidth = EditorIcon::WORLD_SIZE * 0.5f * gizmo.GetSpriteAspect(codepoint);
      float halfHeight = EditorIcon::WORLD_SIZE * 0.5f;
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

    return closest;
  }

  // Walks up to the outermost ancestor. A model imported as forty meshes is one object to
  // work with, so a plain click selects that object and Ctrl drills into the exact mesh
  // the pixel belongs to.
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

  void EditorLayer::DebugDrawGizmos()
  {
    DebugDrawIrradianceVolumeNodes();

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
