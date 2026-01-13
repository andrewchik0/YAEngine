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
        ImGui::Text("Frame time: %.2fms", App().GetTimer().GetDeltaTime() * 1000.0f);
        ImGui::Separator();
        ImGui::DragFloat("Gamma", &App().GetRender().m_Gamma, 0.01f, 0.0f, 10.0f);
        ImGui::DragFloat("Exposure", &App().GetRender().m_Exposure, 0.01f, 0.0f, 10.0f);
        ImGui::InputInt("Current Texture", &App().GetRender().m_CurrentTexture);

        ImGui::Separator();
        for (int i = 0; i < App().GetRender().m_Lights.lightsCount; i++)
        {
          ImGui::DragFloat(("Light cutoff " + std::to_string(i)).c_str(), &App().GetRender().m_Lights.lights[i].cutOff, 0.01f, 0.0f, 120.0f);
          ImGui::DragFloat(("Light outer cutoff " + std::to_string(i)).c_str(), &App().GetRender().m_Lights.lights[i].outerCutOff, 0.01f, 0.0f, 120.0f);
          ImGui::DragFloat3(("Light color " + std::to_string(i)).c_str(), &App().GetRender().m_Lights.lights[i].color.r, 0.01f, -120.0f, 120.0f);
          ImGui::DragFloat3(("Light position " + std::to_string(i)).c_str(), &App().GetRender().m_Lights.lights[i].position.r, 0.01f, -120.0f, 120.0f);
          ImGui::Separator();
        }
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Scene"))
      {
        auto view = App().GetScene().GetView<TransformComponent>();

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

        view.each([&](auto entity, TransformComponent& tc)
        {
          if (tc.parent == entt::null)
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

    auto& tc   = scene.GetComponent<TransformComponent>(entity);
    auto& name = scene.GetComponent<Name>(entity);

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanFullWidth;

    if (tc.firstChild == entt::null)
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

    if (opened && tc.firstChild != entt::null)
    {
      Entity child = tc.firstChild;
      while (child != entt::null)
      {
        DrawEntity(child);
        child = scene.GetComponent<TransformComponent>(child).nextSibling;
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
