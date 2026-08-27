#include "Editor/Panels/OutlinerPanel.h"

#include <imgui.h>

#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorIcons.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/ModelOverrides.h"
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

  // Covers the model root, its nodes, and anything the user parented under them, so the
  // node-authoring items stay available on hand-made children too
  static bool IsInsideModel(Scene& scene, Entity entity)
  {
    for (Entity e = entity; e != entt::null; e = scene.GetHierarchy(e).parent)
    {
      if (scene.HasComponent<ModelSourceComponent>(e))
        return true;
    }
    return false;
  }

  void OutlinerPanel::TakeRevealRequest(EditorContext& context)
  {
    Entity reveal = context.ConsumeRevealRequest();
    if (reveal == entt::null || context.scene == nullptr)
      return;

    if (!context.scene->GetRegistry().valid(reveal))
      return;

    m_RevealTarget = reveal;
    m_RevealChain.clear();
    for (Entity a = context.scene->GetHierarchy(reveal).parent; a != entt::null;
      a = context.scene->GetHierarchy(a).parent)
    {
      m_RevealChain.push_back(a);
    }

    // A target the filter hides would not be drawn at all, and the button would look dead
    m_FilterText[0] = '\0';
    ImGui::SetWindowFocus("Outliner");
  }

  Entity OutlinerPanel::DuplicateModel(EditorContext& context, Entity entity)
  {
    auto& scene = *context.scene;

    // A Model asset points back at exactly one root entity, so a second instance can only
    // come from loading the file again - copying the components would give two roots one asset
    auto& source = scene.GetComponent<ModelSourceComponent>(entity);
    std::string path = source.path;
    bool combined = source.combinedTextures;

    Entity parent = scene.GetHierarchy(entity).parent;
    LocalTransform transform = scene.GetTransform(entity);
    Name name = scene.MakeUniqueEntityName(scene.GetName(entity) + " (Copy)");

    auto handle = context.assetManager->Models().Load(path, combined);
    if (!handle)
    {
      YA_LOG_WARN("Assets", "Duplicate failed: model '%s' could not be loaded", path.c_str());
      return entt::null;
    }

    Entity copy = context.assetManager->Models().Get(handle).rootEntity;
    scene.GetName(copy) = name;
    scene.GetTransform(copy) = transform;

    if (parent != entt::null)
      scene.SetParent(copy, parent);

    scene.MarkDirty(copy);
    return copy;
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
            scene.AddComponent<MaterialComponent>(e, context.assetManager->FindOrCreateDefaultMaterial());
            context.SelectEntity(e);
            BeginRename(context, e);
          }
        }

        ImGui::EndMenu();
      }

      ImGui::Separator();

      if (ImGui::MenuItem(ICON_FA_MOUNTAIN " Terrain"))
      {
        Entity e = scene.CreateEntity("Terrain");
        scene.AddComponent<TerrainComponent>(e);
        scene.AddComponent<MaterialComponent>(e, context.assetManager->FindOrCreateDefaultMaterial());
        scene.GetRegistry().emplace<TerrainDirty>(e);
        context.SelectEntity(e);
        BeginRename(context, e);
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

    // More specific components first: terrain/road/scatter also carry a MeshComponent,
    // so a generic mesh icon would hide what they are. Icons match DetailsPanel's.
    const char* icon = " ";
    if (scene.HasComponent<CameraComponent>(entity))
      icon = ICON_FA_VIDEO;
    else if (scene.HasComponent<LightComponent>(entity))
      icon = ICON_FA_LIGHTBULB;
    else if (scene.HasComponent<ReflectionProbeComponent>(entity))
      icon = ICON_FA_GLOBE;
    else if (scene.HasComponent<IrradianceVolumeComponent>(entity))
      icon = ICON_FA_CUBES;
    else if (scene.HasComponent<TerrainComponent>(entity))
      icon = ICON_FA_MOUNTAIN;
    else if (scene.HasComponent<RoadComponent>(entity))
      icon = ICON_FA_ROAD;
    else if (scene.HasComponent<ScatterComponent>(entity))
      icon = ICON_FA_SEEDLING;
    else if (scene.HasComponent<MeshComponent>(entity))
      icon = ICON_FA_DRAW_POLYGON;
    else if (scene.HasComponent<ColliderComponent>(entity))
      icon = ICON_FA_CUBE;

    // Covers this node only - a collapsed parent does not report overrides in its subtree
    bool overridden = context.componentRegistry != nullptr && context.assetManager != nullptr
      && ModelOverrides::IsNodeOverridden(scene, *context.assetManager, *context.componentRegistry, entity);

    char label[512];
    snprintf(label, sizeof(label), "%s %s%s", icon, name.c_str(), overridden ? "  *" : "");

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

    if (m_RevealTarget != entt::null
      && std::find(m_RevealChain.begin(), m_RevealChain.end(), entity) != m_RevealChain.end())
    {
      ImGui::SetNextItemOpen(true);
    }

    ImGui::SetNextItemAllowOverlap();
    bool opened = ImGui::TreeNodeEx(
      (void*)(uint64_t)entity,
      flags,
      "%s",
      isRenaming ? icon : label
    );

    if (entity == m_RevealTarget)
      ImGui::SetScrollHereY(0.5f);

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

      // Both branches invalidate hc and opened by creating entities, so they finish the
      // popup and the tree node from the values captured here and bail out of the frame
      bool hadChildren = (hc.firstChild != entt::null);

      bool isModelInstance = scene.HasComponent<ModelSourceComponent>(entity);

      if ((isModelInstance || context.componentRegistry != nullptr)
        && ImGui::MenuItem(ICON_FA_CLONE " Duplicate"))
      {
        Entity copy = isModelInstance
          ? DuplicateModel(context, entity)
          : scene.DuplicateEntity(entity, *context.componentRegistry);

        ImGui::EndPopup();
        if (opened && hadChildren)
          ImGui::TreePop();

        if (copy != entt::null)
        {
          context.SelectEntity(copy);
          BeginRename(context, copy);
        }
        return;
      }

      if (IsInsideModel(scene, entity) && ImGui::MenuItem(ICON_FA_SQUARE_PLUS " Add Node"))
      {
        Entity node = scene.CreateEntity(scene.MakeUniqueEntityName("Node"));
        scene.SetParent(node, entity);

        ImGui::EndPopup();
        if (opened && hadChildren)
          ImGui::TreePop();

        context.SelectEntity(node);
        BeginRename(context, node);
        return;
      }

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

        if (!scene.HasComponent<ReflectionProbeComponent>(entity))
        {
          if (ImGui::MenuItem(ICON_FA_GLOBE " Reflection Probe"))
            scene.AddComponent<ReflectionProbeComponent>(entity);
        }

        if (!scene.HasComponent<CameraComponent>(entity))
        {
          if (ImGui::MenuItem(ICON_FA_VIDEO " Camera"))
            scene.AddComponent<CameraComponent>(entity);
        }

        if (!scene.HasComponent<IrradianceVolumeComponent>(entity))
        {
          if (ImGui::MenuItem(ICON_FA_CUBES " Irradiance Volume"))
            scene.AddComponent<IrradianceVolumeComponent>(entity);
        }

        if (!scene.HasComponent<TerrainComponent>(entity))
        {
          if (ImGui::MenuItem(ICON_FA_MOUNTAIN " Terrain"))
          {
            scene.AddComponent<TerrainComponent>(entity);
            if (!scene.HasComponent<MaterialComponent>(entity))
              scene.AddComponent<MaterialComponent>(entity, context.assetManager->FindOrCreateDefaultMaterial());
            scene.GetRegistry().emplace_or_replace<TerrainDirty>(entity);
          }
        }

        ImGui::EndMenu();
      }

      ImGui::Separator();

      bool isModelNode = scene.HasComponent<ModelNodeComponent>(entity)
        && scene.GetComponent<ModelNodeComponent>(entity).nodeIndex != 0;

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

      if (isModelNode && ImGui::IsItemHovered())
        ImGui::SetTooltip("Recorded as a model override and reapplied on load");

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

    // The Create menu above can spawn entities, which reallocates the hierarchy pool and
    // leaves the hc taken at the top of this function dangling
    auto& tailHc = scene.GetComponent<HierarchyComponent>(entity);

    if (opened && tailHc.firstChild != entt::null)
    {
      Entity child = tailHc.firstChild;
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
    TakeRevealRequest(context);

    // A pending reveal survives a collapsed window: the request above already pulled the
    // panel forward, and the chain is only cleared once it has actually been drawn
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

    if (m_RevealTarget != entt::null)
      ImGui::SetNextItemOpen(true);

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

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
      context.ClearSelection();

    m_RevealTarget = entt::null;
    m_RevealChain.clear();

    ImGui::End();
  }
}
