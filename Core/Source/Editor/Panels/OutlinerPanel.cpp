#include "Editor/Panels/OutlinerPanel.h"

#include <imgui.h>

#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorIcons.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Assets/AssetManager.h"
#include "Editor/Utils/FileDialog.h"

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

  static MaterialHandle FindOrCreateDefaultMaterial(AssetManager& assets)
  {
    MaterialHandle found = MaterialHandle::Invalid();
    assets.Materials().ForEachWithHandle([&](MaterialHandle handle, Material& mat)
    {
      if (!found && mat.name == "Default")
        found = handle;
    });
    if (found)
      return found;

    auto handle = assets.Materials().Create();
    assets.Materials().Get(handle).name = "Default";
    return handle;
  }

  void OutlinerPanel::BeginRename(EditorContext& context, Entity entity)
  {
    m_RenamingEntity = entity;
    auto& scene = *context.scene;
    std::string name = scene.HasComponent<Name>(entity) ? scene.GetName(entity) : "";
    snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", name.c_str());
    b_RenameNeedsFocus = true;
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
        BeginRename(context, e);
      }

      ImGui::Separator();

      if (ImGui::MenuItem(ICON_FA_LIGHTBULB " Point Light"))
      {
        Entity e = scene.CreateEntity("PointLight");
        scene.AddComponent<LightComponent>(e, LightType::Point);
        context.SelectEntity(e);
        BeginRename(context, e);
      }

      if (ImGui::MenuItem(ICON_FA_LIGHTBULB " Spot Light"))
      {
        Entity e = scene.CreateEntity("SpotLight");
        scene.AddComponent<LightComponent>(e, LightType::Spot);
        context.SelectEntity(e);
        BeginRename(context, e);
      }

      if (ImGui::MenuItem(ICON_FA_SUN " Directional Light"))
      {
        Entity e = scene.CreateEntity("DirectionalLight");
        scene.AddComponent<LightComponent>(e, LightType::Directional);
        context.SelectEntity(e);
        BeginRename(context, e);
      }

      ImGui::Separator();

      if (ImGui::BeginMenu(ICON_FA_SHAPES " Primitives"))
      {
        struct PrimitiveEntry { const char* icon; const char* label; const char* name; PrimitiveType type; };
        PrimitiveEntry primitives[] = {
          { ICON_FA_CUBE,   " Cube",   "Cube",   PrimitiveType::Box },
          { ICON_FA_CIRCLE, " Sphere", "Sphere", PrimitiveType::Sphere },
          { ICON_FA_SQUARE, " Plane",  "Plane",  PrimitiveType::Plane },
        };

        for (auto& [pIcon, pLabel, pName, pType] : primitives)
        {
          char menuLabel[64];
          snprintf(menuLabel, sizeof(menuLabel), "%s%s", pIcon, pLabel);
          if (ImGui::MenuItem(menuLabel))
          {
            Entity e = scene.CreateEntity(pName);
            scene.AddComponent<MeshComponent>(e, context.assetManager->Primitives().Create(pType));
            scene.AddComponent<MaterialComponent>(e, FindOrCreateDefaultMaterial(*context.assetManager));
            context.SelectEntity(e);
            BeginRename(context, e);
          }
        }

        ImGui::EndMenu();
      }

      ImGui::Separator();

      if (ImGui::MenuItem(ICON_FA_FILE_IMPORT " Import Model..."))
      {
        nfdu8filteritem_t filters[] = {
          { "3D Models", "gltf,glb,obj,fbx" },
        };
        std::string path = FileDialog::OpenFile(filters, 1);
        if (!path.empty())
        {
          auto handle = context.assetManager->Models().Load(path);
          if (handle)
          {
            auto& model = context.assetManager->Models().Get(handle);
            context.SelectEntity(model.rootEntity);
          }
        }
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
    else if (scene.HasComponent<LightProbeComponent>(entity))
      icon = ICON_FA_GLOBE;

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

    bool isRenaming = (m_RenamingEntity == entity);

    if (isRenaming)
      flags |= ImGuiTreeNodeFlags_AllowOverlap;

    ImGui::SetNextItemAllowOverlap();
    bool opened = ImGui::TreeNodeEx(
      (void*)(uint64_t)entity,
      flags,
      "%s",
      isRenaming ? icon : label
    );

    if (isRenaming)
    {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
      if (b_RenameNeedsFocus)
        ImGui::SetKeyboardFocusHere();

      if (ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
      {
        if (m_RenameBuffer[0] != '\0')
          scene.GetName(entity) = m_RenameBuffer;
        m_RenamingEntity = entt::null;
        b_RenameNeedsFocus = false;
      }
      else if (ImGui::IsKeyPressed(ImGuiKey_Escape))
      {
        m_RenamingEntity = entt::null;
        b_RenameNeedsFocus = false;
      }
      else if (b_RenameNeedsFocus)
      {
        if (ImGui::IsItemActive())
          b_RenameNeedsFocus = false;
      }
      else if (!ImGui::IsItemActive() && !ImGui::IsItemFocused())
      {
        if (m_RenameBuffer[0] != '\0')
          scene.GetName(entity) = m_RenameBuffer;
        m_RenamingEntity = entt::null;
      }
    }
    else
    {
      if (ImGui::IsItemClicked())
        context.SelectEntity(entity);

      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        BeginRename(context, entity);
    }

    char popupId[64];
    snprintf(popupId, sizeof(popupId), "##entity_ctx_%u", static_cast<uint32_t>(entity));
    if (ImGui::BeginPopupContextItem(popupId))
    {
      if (ImGui::MenuItem(ICON_FA_PEN " Rename"))
        BeginRename(context, entity);

      if (scene.HasComponent<MeshComponent>(entity))
      {
        bool visible = !scene.HasComponent<HiddenTag>(entity);
        if (ImGui::MenuItem(visible ? ICON_FA_EYE_SLASH " Hide" : ICON_FA_EYE " Show"))
        {
          if (visible) scene.AddComponent<HiddenTag>(entity);
          else scene.RemoveComponent<HiddenTag>(entity);
        }
      }

      ImGui::Separator();

      DrawCreateMenu(context);

      if (ImGui::BeginMenu(ICON_FA_CIRCLE_PLUS " Add Component"))
      {
        if (!scene.HasComponent<LightComponent>(entity))
        {
          if (ImGui::MenuItem(ICON_FA_LIGHTBULB " Point Light"))
            scene.AddComponent<LightComponent>(entity, LightType::Point);

          if (ImGui::MenuItem(ICON_FA_LIGHTBULB " Spot Light"))
            scene.AddComponent<LightComponent>(entity, LightType::Spot);

          if (ImGui::MenuItem(ICON_FA_SUN " Directional Light"))
            scene.AddComponent<LightComponent>(entity, LightType::Directional);
        }

        if (!scene.HasComponent<LightProbeComponent>(entity))
        {
          if (ImGui::MenuItem(ICON_FA_GLOBE " Light Probe"))
            scene.AddComponent<LightProbeComponent>(entity);
        }

        if (!scene.HasComponent<CameraComponent>(entity))
        {
          if (ImGui::MenuItem(ICON_FA_VIDEO " Camera"))
            scene.AddComponent<CameraComponent>(entity);
        }

        ImGui::EndMenu();
      }

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
