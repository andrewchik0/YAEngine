#include "Editor/EditorLayer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include "Application.h"
#include "Editor/Utils/EditorStyle.h"
#include "Editor/Panels/ViewportPanel.h"
#include "Editor/Panels/PerformancePanel.h"
#include "Editor/Panels/RenderSettingsPanel.h"
#include "Editor/Panels/OutlinerPanel.h"
#include "Editor/Panels/DetailsPanel.h"
#include "Editor/EditorCameraLayer.h"

namespace YAEngine
{
  void EditorLayer::OnAttach()
  {
    EditorStyle::Apply();
    App().PushLayer<EditorCameraLayer>();
    m_Panels.push_back(std::make_unique<ViewportPanel>());
    m_Panels.push_back(std::make_unique<OutlinerPanel>());
    m_Panels.push_back(std::make_unique<DetailsPanel>());
    m_Panels.push_back(std::make_unique<RenderSettingsPanel>());
    m_Panels.push_back(std::make_unique<PerformancePanel>());
  }

  void EditorLayer::OnSceneReady()
  {
    m_Context.scene = &App().GetScene();
    m_Context.assetManager = &App().GetAssetManager();
    m_Context.render = &App().GetRender();

    for (auto& panel : m_Panels)
      panel->OnSceneReady(m_Context);
  }

  void EditorLayer::RenderUI()
  {
    ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    if (!m_LayoutBuilt)
    {
      if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr ||
          ImGui::DockBuilderGetNode(dockspaceId)->IsSplitNode() == false)
      {
        BuildDefaultLayout(dockspaceId);
      }
      m_LayoutBuilt = true;
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
          m_LayoutBuilt = false;
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

    App().GetInputSystem().SetViewportHovered(m_Context.viewportHovered);
  }

  void EditorLayer::OnDetach()
  {
    m_Panels.clear();
  }

  void EditorLayer::BuildDefaultLayout(ImGuiID dockspaceId)
  {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

    ImGuiID dockLeft;
    ImGuiID dockRemaining;
    ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.15f, &dockLeft, &dockRemaining);

    ImGuiID dockBottom;
    ImGuiID dockCenterRight;
    ImGui::DockBuilderSplitNode(dockRemaining, ImGuiDir_Down, 0.25f, &dockBottom, &dockCenterRight);

    ImGuiID dockCenter;
    ImGuiID dockRight;
    ImGui::DockBuilderSplitNode(dockCenterRight, ImGuiDir_Right, 0.30f, &dockRight, &dockCenter);

    ImGui::DockBuilderDockWindow("Outliner", dockLeft);
    ImGui::DockBuilderDockWindow("Viewport", dockCenter);
    ImGui::DockBuilderDockWindow("Details", dockRight);
    ImGui::DockBuilderDockWindow("Render Settings", dockRight);
    ImGui::DockBuilderDockWindow("Debug Viz", dockRight);
    ImGui::DockBuilderDockWindow("Performance", dockBottom);
    ImGui::DockBuilderDockWindow("Console", dockBottom);
    ImGui::DockBuilderDockWindow("Content Browser", dockBottom);

    ImGui::DockBuilderFinish(dockspaceId);
  }
}
