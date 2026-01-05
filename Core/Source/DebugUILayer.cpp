#include "DebugUILayer.h"

#include <imgui.h>

#include "Application.h"

namespace YAEngine
{
  void DebugUILayer::RenderUI()
  {
    ImGui::Begin("Debug UI");

    if (ImGui::BeginTabBar("###TabBar"))
    {
      if (ImGui::BeginTabItem("Info"))
      {
        static float updateTime = 0.0f;
        static float fps = App().GetTimer().GetFPS();
        updateTime += App().GetTimer().GetDeltaTime();

        if (updateTime >= .5f)
        {
          fps = App().GetTimer().GetFPS();
        }

        ImGui::Text("FPS: %.1f", App().GetTimer().GetFPS());
        ImGui::Text("Frame time: %.2fms", App().GetTimer().GetDeltaTime() * 1000.0f);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Scene"))
      {
        auto view = App().GetScene().GetView<TransformComponent>();

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
    auto& tc = scene.GetComponent<TransformComponent>(entity);
    const auto& name = scene.GetComponent<Name>(entity);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
    if (tc.firstChild == entt::null)
      flags |= ImGuiTreeNodeFlags_Leaf;

    bool opened = ImGui::TreeNodeEx(
      (void*)(uint64_t)entity,
      flags,
      "%s",
      name.c_str()
    );

    if (scene.HasComponent<MeshComponent>(entity))
    {
      ImGui::SameLine();
      ImGui::Checkbox(
        "##render",
        &scene.GetComponent<MeshComponent>(entity).shouldRender
      );
    }

    if (opened)
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
}
