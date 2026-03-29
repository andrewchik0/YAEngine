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

    m_TextureCache.Init(m_Context.assetManager);
    m_Context.textureCache = &m_TextureCache;

    for (auto& panel : m_Panels)
      panel->OnSceneReady(m_Context);
  }

  void EditorLayer::Update(double deltaTime)
  {
    uint32_t w = m_Context.viewportWidth;
    uint32_t h = m_Context.viewportHeight;
    if (w > 0 && h > 0 && (w != m_LastViewportWidth || h != m_LastViewportHeight))
    {
      m_LastViewportWidth = w;
      m_LastViewportHeight = h;
      for (auto [entity, cam] : GetScene().GetView<CameraComponent>().each())
        cam.Resize(float(w), float(h));
    }

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

    if (ImGui::BeginMainMenuBar())
    {
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

    for (auto& panel : m_Panels)
    {
      if (panel->IsVisible())
        panel->OnRender(m_Context);
    }

    GetInput().SetViewportHovered(m_Context.viewportHovered);
  }

  void EditorLayer::OnDetach()
  {
    GetRender().WaitIdle();
    m_Panels.clear();
    m_TextureCache.Destroy();
    FileDialog::Shutdown();
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
