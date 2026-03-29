#include "Editor/Panels/OutlinerPanel.h"

#include <imgui.h>

#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorIcons.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"

namespace YAEngine
{
  static bool ContainsCI(std::string_view haystack, std::string_view needle)
  {
    if (needle.size() > haystack.size()) return false;
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
      [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
      });
    return it != haystack.end();
  }

  bool OutlinerPanel::MatchesFilter(EditorContext& context, Entity entity)
  {
    if (m_FilterText[0] == '\0')
      return true;

    auto& scene = *context.scene;

    if (scene.HasComponent<Name>(entity))
    {
      const auto& name = scene.GetName(entity);
      if (ContainsCI(name, m_FilterText))
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
      if (ImGui::MenuItem(ICON_FA_CUBE " Empty Entity"))
      {
        Entity e = scene.CreateEntity("Entity");
        context.SelectEntity(e);
      }

      ImGui::Separator();

      if (ImGui::MenuItem(ICON_FA_LIGHTBULB " Point Light"))
      {
        Entity e = scene.CreateEntity("PointLight");
        scene.AddComponent<LightComponent>(e, LightType::Point);
        context.SelectEntity(e);
      }

      if (ImGui::MenuItem(ICON_FA_LIGHTBULB " Spot Light"))
      {
        Entity e = scene.CreateEntity("SpotLight");
        scene.AddComponent<LightComponent>(e, LightType::Spot);
        context.SelectEntity(e);
      }

      if (ImGui::MenuItem(ICON_FA_SUN " Directional Light"))
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
    if (scene.HasComponent<CameraComponent>(entity))
      icon = ICON_FA_VIDEO;
    else if (scene.HasComponent<MeshComponent>(entity))
      icon = ICON_FA_DRAW_POLYGON;
    else if (scene.HasComponent<LightComponent>(entity))
      icon = ICON_FA_LIGHTBULB;

    char label[512];
    snprintf(label, sizeof(label), "%s %s", icon, name.c_str());

    ImGuiTreeNodeFlags flags =
      ImGuiTreeNodeFlags_OpenOnArrow |
      ImGuiTreeNodeFlags_SpanFullWidth |
      ImGuiTreeNodeFlags_AllowOverlap;

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

    char popupId[64];
    snprintf(popupId, sizeof(popupId), "##entity_ctx_%u", static_cast<uint32_t>(entity));
    if (ImGui::BeginPopupContextItem(popupId))
    {
      if (scene.HasComponent<MeshComponent>(entity))
      {
        bool visible = !scene.HasComponent<HiddenTag>(entity);
        if (ImGui::MenuItem(visible ? ICON_FA_EYE_SLASH " Hide" : ICON_FA_EYE " Show"))
        {
          if (visible) scene.AddComponent<HiddenTag>(entity);
          else scene.RemoveComponent<HiddenTag>(entity);
        }

        ImGui::Separator();
      }

      DrawCreateMenu(context);

      ImGui::Separator();

      if (ImGui::MenuItem(ICON_FA_TRASH_CAN " Delete"))
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
      if (ImGui::SmallButton(visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH))
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
    ImGui::InputTextWithHint("##filter", ICON_FA_MAGNIFYING_GLASS " Search...", m_FilterText, sizeof(m_FilterText));

    ImGui::Separator();

    ImGuiTreeNodeFlags sceneFlags =
      ImGuiTreeNodeFlags_DefaultOpen |
      ImGuiTreeNodeFlags_OpenOnArrow |
      ImGuiTreeNodeFlags_SpanFullWidth;

    if (ImGui::TreeNodeEx(ICON_FA_GLOBE " Scene", sceneFlags))
    {
      auto hierarchyView = context.scene->GetView<LocalTransform, HierarchyComponent>();
      hierarchyView.each([&](auto entity, LocalTransform&, HierarchyComponent& hc)
      {
        if (hc.parent == entt::null)
          DrawEntity(context, entity);
      });

      ImGui::TreePop();
    }

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
