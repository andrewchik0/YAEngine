#include "DebugUILayer.h"

#include <imgui.h>

#include "Application.h"

namespace YAEngine
{
  static Entity s_SelectedEntity;

  void DebugUILayer::RenderUI()
  {
    ImGui::Begin("Debug UI");
    ImGui::SetWindowFontScale(1.5);

    if (ImGui::BeginTabBar("###TabBar"))
    {
      if (ImGui::BeginTabItem("Info"))
      {
        static float updateTime = 0.0f;
        static float fps;
        updateTime += (float)App().GetTimer().GetDeltaTime();

        if (updateTime >= .5f)
        {
          fps = App().GetTimer().GetFPS();
          updateTime = 0.0f;
        }

        ImGui::Text("FPS: %.1f", fps);

        float frametime = (float)App().GetTimer().GetDeltaTime() * 1000.0f;
        ImGui::Text("Frame time: %.2fms", frametime);

        m_FrametimeHistory[m_FrametimeOffset] = frametime;
        m_FrametimeOffset = (m_FrametimeOffset + 1) % FRAMETIME_HISTORY_SIZE;

        float maxFrametime = 0.0f;
        for (int i = 0; i < FRAMETIME_HISTORY_SIZE; i++)
          if (m_FrametimeHistory[i] > maxFrametime)
            maxFrametime = m_FrametimeHistory[i];

        char overlay[32];
        snprintf(overlay, sizeof(overlay), "%.2f ms", frametime);
        ImGui::PlotLines("##frametime", m_FrametimeHistory, FRAMETIME_HISTORY_SIZE,
          m_FrametimeOffset, overlay, 0.0f, maxFrametime * 1.2f, ImVec2(0, 80));

        ImGui::Separator();
        ImGui::DragFloat("Gamma", &App().GetRender().GetGamma(), 0.01f, 0.0f, 10.0f);
        ImGui::DragFloat("Exposure", &App().GetRender().GetExposure(), 0.01f, 0.0f, 10.0f);
        {
          static int debugViewIndex = 0;
          const char* debugViews[] = {
            "Off", "Albedo", "Metallic", "Roughness", "Normals", "SSAO", "SSR"
          };
          if (ImGui::Combo("Debug View", &debugViewIndex, debugViews, IM_ARRAYSIZE(debugViews)))
            App().GetRender().SetDebugView(debugViewIndex);
        }

        ImGui::Checkbox("SSAO", &App().GetRender().GetSSAOEnabled());
        ImGui::Checkbox("SSR", &App().GetRender().GetSSREnabled());
        ImGui::Checkbox("TAA", &App().GetRender().GetTAAEnabled());
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Scene"))
      {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
        DrawInspector();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::Text("Scene Tree");
        ImGui::GetWindowDrawList()->AddText(
          ImVec2(pos.x + 1, pos.y),
          ImGui::GetColorU32(ImGuiCol_Text),
          "Scene Tree"
        );

        auto hierarchyView = App().GetScene().GetView<TransformComponent, HierarchyComponent>();
        hierarchyView.each([&](auto entity, TransformComponent& tc, HierarchyComponent& hc)
        {
          if (hc.parent == entt::null)
          {
            DrawEntity(entity);
          }
        });
        ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
    }

    ImGui::End();
  }

  void DebugUILayer::DrawEntity(Entity entity)
  {
    auto& scene = App().GetScene();

    auto& hc = scene.GetComponent<HierarchyComponent>(entity);
    Name name = std::to_string(int(entity));
    if (scene.HasComponent<Name>(entity))
      name = scene.GetName(entity);

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanFullWidth;

    if (hc.firstChild == entt::null)
      flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    if (entity == s_SelectedEntity)
      flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::SetNextItemWidth(400.0f);
    bool opened = ImGui::TreeNodeEx(
        (void*)(uint64_t)entity,
        flags,
        "%s",
        name.c_str()
    );

    if (ImGui::IsItemClicked())
      s_SelectedEntity = entity;

    if (opened && hc.firstChild != entt::null)
    {
      Entity child = hc.firstChild;
      while (child != entt::null)
      {
        DrawEntity(child);
        child = scene.GetComponent<HierarchyComponent>(child).nextSibling;
      }

      ImGui::TreePop();
    }
  }


  void DebugUILayer::DrawTransform(TransformComponent& tc)
  {
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
      bool dirty = false;
      if (ImGui::DragFloat3("Position", &tc.position.x, 0.1f))
        dirty = true;

      glm::vec3 euler = glm::degrees(glm::eulerAngles(tc.rotation));
      if (ImGui::DragFloat3("Rotation", &euler.x, 0.5f))
      {
        tc.rotation = glm::quat(glm::radians(euler));
        dirty = true;
      }

      if (ImGui::DragFloat3("Scale", &tc.scale.x, 0.1f, 0.001f, 1000.0f))
        dirty = true;

      ImGui::Text("MinBB: (%.1f, %.1f, %.1f)", tc.minBB.x, tc.minBB.y, tc.minBB.z);
      ImGui::Text("MaxBB: (%.1f, %.1f, %.1f)", tc.maxBB.x, tc.maxBB.y, tc.maxBB.z);

      if (dirty)
        App().GetScene().MarkDirty(s_SelectedEntity);
    }
  }

  void DebugUILayer::DrawInspector()
  {
    if (s_SelectedEntity == entt::null)
      return;

    auto& scene = App().GetScene();

    if (scene.HasComponent<TransformComponent>(s_SelectedEntity))
    {
      DrawTransform(scene.GetComponent<TransformComponent>(s_SelectedEntity));
    }

    if (scene.HasComponent<MeshComponent>(s_SelectedEntity))
    {
      auto& meshComponent = scene.GetComponent<MeshComponent>(s_SelectedEntity);
      ImGui::Checkbox(
        "Render",
        &meshComponent.shouldRender
      );
      ImGui::Text("Double sided: %i", meshComponent.doubleSided);
      ImGui::Text("No shading: %i", meshComponent.noShading);

      auto mesh = App().GetAssetManager().Meshes().Get(meshComponent.asset);
      if (mesh.instanceData != nullptr)
      {
        ImGui::Text("Instance count: %i", mesh.instanceData->size());
        ImGui::Text("Offset: %i", mesh.offset);
      }
    }
  }
}
