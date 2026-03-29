#include "Editor/Panels/OutlinerPanel.h"

#include <imgui.h>

#include "Editor/EditorContext.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"

namespace YAEngine
{
  static std::string ToLower(std::string_view str)
  {
    std::string result(str);
    for (auto& c : result)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
  }

  bool OutlinerPanel::MatchesFilter(EditorContext& context, Entity entity)
  {
    if (m_FilterText[0] == '\0')
      return true;

    auto& scene = *context.scene;

    if (scene.HasComponent<Name>(entity))
    {
      const auto& name = scene.GetName(entity);
      if (ToLower(name).find(ToLower(m_FilterText)) != std::string::npos)
        return true;
    }

    auto& hc = scene.GetComponent<HierarchyComponent>(entity);
    Entity child = hc.firstChild;
    while (child != entt::null)
    {
      if (MatchesFilter(context, child))
        return true;
      child = scene.GetComponent<HierarchyComponent>(child).nextSibling;
    }

    return false;
  }

  void OutlinerPanel::DrawCreateMenu(EditorContext& context)
  {
    auto& scene = *context.scene;

    if (ImGui::BeginMenu("Create"))
    {
      if (ImGui::MenuItem("Empty Entity"))
      {
        Entity e = scene.CreateEntity("Entity");
        context.SelectEntity(e);
      }

      ImGui::Separator();

      if (ImGui::MenuItem("Point Light"))
      {
        Entity e = scene.CreateEntity("PointLight");
        scene.AddComponent<LightComponent>(e, LightType::Point);
        context.SelectEntity(e);
      }

      if (ImGui::MenuItem("Spot Light"))
      {
        Entity e = scene.CreateEntity("SpotLight");
        scene.AddComponent<LightComponent>(e, LightType::Spot);
        context.SelectEntity(e);
      }

      if (ImGui::MenuItem("Directional Light"))
      {
        Entity e = scene.CreateEntity("DirectionalLight");
        scene.AddComponent<LightComponent>(e, LightType::Directional);
        context.SelectEntity(e);
      }

      ImGui::EndMenu();
    }
  }

  void OutlinerPanel::DrawEntity(EditorContext& context, Entity entity)
  {
    auto& scene = *context.scene;

    if (scene.HasComponent<EditorOnlyTag>(entity))
      return;

    if (!MatchesFilter(context, entity))
      return;

    auto& hc = scene.GetComponent<HierarchyComponent>(entity);
    Name name = std::to_string((int)entity);
    if (scene.HasComponent<Name>(entity))
      name = scene.GetName(entity);

    const char* icon = " ";
    bool isLight = false;
    if (scene.HasComponent<CameraComponent>(entity))
      icon = "[C]";
    else if (scene.HasComponent<MeshComponent>(entity))
      icon = "[M]";
    else if (scene.HasComponent<LightComponent>(entity))
    {
      icon = "   ";
      isLight = true;
    }

    char label[512];
    snprintf(label, sizeof(label), "%s %s", icon, name.c_str());

    ImGuiTreeNodeFlags flags =
      ImGuiTreeNodeFlags_OpenOnArrow |
      ImGuiTreeNodeFlags_SpanFullWidth;

    if (hc.firstChild == entt::null)
      flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    if (entity == context.selectedEntity)
      flags |= ImGuiTreeNodeFlags_Selected;

    bool opened = ImGui::TreeNodeEx(
      (void*)(uint64_t)entity,
      flags,
      "%s",
      label
    );

    if (ImGui::IsItemClicked())
      context.SelectEntity(entity);

    if (isLight)
    {
      ImGui::SameLine(ImGui::GetTreeNodeToLabelSpacing());
      ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "[L]");
    }

    char popupId[64];
    snprintf(popupId, sizeof(popupId), "##entity_ctx_%u", static_cast<uint32_t>(entity));
    if (ImGui::BeginPopupContextItem(popupId))
    {
      if (scene.HasComponent<MeshComponent>(entity))
      {
        bool visible = !scene.HasComponent<HiddenTag>(entity);
        if (ImGui::MenuItem(visible ? "Hide" : "Show"))
        {
          if (visible) scene.AddComponent<HiddenTag>(entity);
          else scene.RemoveComponent<HiddenTag>(entity);
        }

        ImGui::Separator();
      }

      DrawCreateMenu(context);

      ImGui::Separator();

      if (ImGui::MenuItem("Delete"))
      {
        if (context.selectedEntity == entity)
          context.ClearSelection();
        bool hadChildren = (hc.firstChild != entt::null);
        scene.DestroyEntity(entity);
        ImGui::EndPopup();
        if (opened && hadChildren)
          ImGui::TreePop();
        return;
      }

      ImGui::EndPopup();
    }

    if (scene.HasComponent<MeshComponent>(entity))
    {
      bool visible = !scene.HasComponent<HiddenTag>(entity);
      ImGui::SameLine(ImGui::GetWindowWidth() - 40);
      ImGui::PushID((void*)(uintptr_t)entity);
      ImGui::PushID("vis");
      if (ImGui::SmallButton(visible ? "o" : "-"))
      {
        if (visible) scene.AddComponent<HiddenTag>(entity);
        else scene.RemoveComponent<HiddenTag>(entity);
      }
      ImGui::PopID();
      ImGui::PopID();
    }

    if (opened && hc.firstChild != entt::null)
    {
      Entity child = hc.firstChild;
      while (child != entt::null)
      {
        auto& childHc = scene.GetComponent<HierarchyComponent>(child);
        Entity next = childHc.nextSibling;
        DrawEntity(context, child);
        child = next;
      }

      ImGui::TreePop();
    }
  }

  void OutlinerPanel::OnRender(EditorContext& context)
  {
    if (!ImGui::Begin("Outliner"))
    {
      ImGui::End();
      return;
    }

    if (!context.scene)
    {
      ImGui::TextDisabled("Scene not available");
      ImGui::End();
      return;
    }

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "Search...", m_FilterText, sizeof(m_FilterText));

    ImGui::Separator();

    auto hierarchyView = context.scene->GetView<LocalTransform, HierarchyComponent>();
    hierarchyView.each([&](auto entity, LocalTransform&, HierarchyComponent& hc)
    {
      if (hc.parent == entt::null)
        DrawEntity(context, entity);
    });

    if (ImGui::BeginPopupContextWindow("##OutlinerContextWindow", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight))
    {
      DrawCreateMenu(context);
      ImGui::EndPopup();
    }

    // Click empty space to deselect
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
      context.ClearSelection();

    ImGui::End();
  }
}
