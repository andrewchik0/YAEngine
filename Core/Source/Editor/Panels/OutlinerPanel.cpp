#include "Editor/Panels/OutlinerPanel.h"

#include <imgui.h>
#include <imgui_internal.h>

#include "Editor/EditorCommands.h"
#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorIcons.h"
#include "Editor/Utils/EditorStyle.h"
#include "Editor/Utils/EditorWidgets.h"
#include "Editor/Utils/FileDialog.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/ModelOverrides.h"
#include "Assets/AssetManager.h"
#include "Utils/StringSearch.h"

namespace YAEngine
{
  namespace
  {
    // Integer id scope for a row's fixed widgets (expand button, rename field, visibility button, context
    // menu). An entity name is hashed as a string in the row scope, so no typed name can land on them.
    constexpr int32_t ROW_WIDGET_SCOPE = 1;

    ImU32 ThemeColor(const glm::vec4& color)
    {
      return ImGui::GetColorU32(ToImGuiColor(color));
    }

    // Covers the model root, its nodes, and anything the user parented under them, so the
    // node-authoring items stay available on hand-made children too
    bool IsInsideModel(Scene& scene, Entity entity)
    {
      for (Entity e = entity; e != entt::null; e = scene.GetHierarchy(e).parent)
      {
        if (scene.HasComponent<ModelSourceComponent>(e))
          return true;
      }
      return false;
    }

    // More specific components first: terrain/road/scatter also carry a MeshComponent,
    // so a generic mesh icon would hide what they are. Icons match DetailsPanel's.
    const char* GetEntityIcon(const entt::registry& registry, Entity entity)
    {
      if (registry.all_of<CameraComponent>(entity))
        return ICON_LC_VIDEO;
      if (registry.all_of<LightComponent>(entity))
        return ICON_LC_LIGHTBULB;
      if (registry.all_of<ReflectionProbeComponent>(entity))
        return ICON_LC_GLOBE;
      if (registry.all_of<IrradianceVolumeComponent>(entity))
        return ICON_LC_BOXES;
      if (registry.all_of<TerrainComponent>(entity))
        return ICON_LC_MOUNTAIN;
      if (registry.all_of<RoadComponent>(entity))
        return ICON_LC_ROUTE;
      if (registry.all_of<ScatterComponent>(entity))
        return ICON_LC_SPROUT;
      if (registry.all_of<ModelSourceComponent>(entity))
        return ICON_LC_PACKAGE;
      if (registry.all_of<MeshComponent>(entity))
        return ICON_LC_PYRAMID;
      if (registry.all_of<ColliderComponent>(entity))
        return ICON_LC_BOX;
      return nullptr;
    }

    void ToggleHidden(Scene& scene, Entity entity)
    {
      if (scene.HasComponent<HiddenTag>(entity))
        scene.RemoveComponent<HiddenTag>(entity);
      else
        scene.AddComponent<HiddenTag>(entity);
    }

    // Spreads an entity id over 64 bits (splitmix64), so a sum of them tells one set of roots from another
    uint64_t HashEntity(Entity entity)
    {
      uint64_t x = uint64_t(entt::to_integral(entity)) + 0x9E3779B97F4A7C15ull;
      x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
      x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
      return x ^ (x >> 31);
    }
  }

  void OutlinerPanel::OnSceneReady(EditorContext& context)
  {
    // A search typed for the previous scene would hide most of this one
    m_FilterText[0] = '\0';
    m_MatchedFilter.clear();
    b_MatchesDirty = true;
    m_Matches.clear();
    m_FilterVisible.clear();
    m_SearchCollapsed.clear();
    m_Expanded.clear();
    m_RootOrder.clear();
    m_NextRootOrder = 0;
    m_Roots.clear();
    m_RootSignature = 0;
    m_Rows.clear();
    m_RenamingEntity = entt::null;
    b_RenameNeedsFocus = false;
    m_LastSelection = context.selectedEntity;
    m_ScrollTarget = entt::null;
    m_MenuEntity = entt::null;
    m_PendingAction = PendingAction::None;
    m_PendingEntity = entt::null;
  }

  void OutlinerPanel::TakeRevealRequest(EditorContext& context)
  {
    Entity reveal = context.ConsumeRevealRequest();
    if (reveal == entt::null || context.scene == nullptr)
      return;

    if (!context.scene->GetRegistry().valid(reveal))
      return;

    // A target the filter hides would not be drawn at all, and the button would look dead
    m_FilterText[0] = '\0';
    Reveal(*context.scene, reveal);
    // The request selects the entity too; that selection is already being revealed here
    m_LastSelection = context.selectedEntity;
    RequestFocus();
  }

  void OutlinerPanel::FollowSelection(EditorContext& context)
  {
    Entity selected = context.selectedEntity;
    if (selected == m_LastSelection)
      return;

    m_LastSelection = selected;
    if (selected != entt::null && context.scene->GetRegistry().valid(selected))
      Reveal(*context.scene, selected);
  }

  void OutlinerPanel::Reveal(Scene& scene, Entity entity)
  {
    entt::registry& registry = scene.GetRegistry();
    const HierarchyComponent* hierarchy = registry.try_get<HierarchyComponent>(entity);
    Entity ancestor = hierarchy != nullptr ? hierarchy->parent : Entity(entt::null);
    while (ancestor != entt::null && registry.valid(ancestor))
    {
      m_Expanded.insert(ancestor);
      m_SearchCollapsed.erase(ancestor);
      const HierarchyComponent* ancestorHierarchy = registry.try_get<HierarchyComponent>(ancestor);
      ancestor = ancestorHierarchy != nullptr ? ancestorHierarchy->parent : Entity(entt::null);
    }
    m_ScrollTarget = entity;
  }

  void OutlinerPanel::SetOpen(Entity entity, bool open)
  {
    if (IsFiltering())
    {
      if (open)
        m_SearchCollapsed.erase(entity);
      else
        m_SearchCollapsed.insert(entity);
    }
    else if (open)
    {
      m_Expanded.insert(entity);
    }
    else
    {
      m_Expanded.erase(entity);
    }
  }

  void OutlinerPanel::UpdateRootOrder(Scene& scene)
  {
    entt::registry& registry = scene.GetRegistry();

    m_RootScratch.clear();
    uint64_t signature = 0;
    for (Entity entity : registry.view<LocalTransform, HierarchyComponent>())
    {
      if (registry.get<HierarchyComponent>(entity).parent != entt::null || registry.all_of<EditorOnlyTag>(entity))
        continue;

      m_RootScratch.push_back(entity);
      signature += HashEntity(entity);
    }

    // Sorting looks every root up in m_RootOrder, so it waits for the set of roots to change
    if (m_RootScratch.size() == m_Roots.size() && signature == m_RootSignature)
      return;

    m_RootSignature = signature;
    m_Roots.swap(m_RootScratch);

    std::vector<Entity> unseen;
    for (Entity entity : m_Roots)
    {
      if (!m_RootOrder.contains(entity))
        unseen.push_back(entity);
    }

    if (!unseen.empty())
    {
      // Every entity gets its LocalTransform when it is created, so that pool's packing order is the
      // creation order of the entities seen together here - a whole scene on load
      auto& transforms = registry.storage<LocalTransform>();
      std::sort(unseen.begin(), unseen.end(),
        [&transforms](Entity a, Entity b) { return transforms.index(a) < transforms.index(b); });
      for (Entity entity : unseen)
        m_RootOrder.emplace(entity, m_NextRootOrder++);
    }

    std::sort(m_Roots.begin(), m_Roots.end(),
      [this](Entity a, Entity b) { return m_RootOrder.at(a) < m_RootOrder.at(b); });

    if (m_RootOrder.size() > m_Roots.size() * 2 + 64)
      std::erase_if(m_RootOrder, [&registry](const auto& entry) { return !registry.valid(entry.first); });
  }

  void OutlinerPanel::UpdateFilter(Scene& scene)
  {
    if (!IsFiltering())
    {
      if (!m_MatchedFilter.empty())
      {
        m_MatchedFilter.clear();
        m_Matches.clear();
        m_FilterVisible.clear();
        m_SearchCollapsed.clear();
      }
      return;
    }

    entt::registry& registry = scene.GetRegistry();
    const size_t entityCount = registry.storage<HierarchyComponent>().size();
    const uint64_t generation = scene.GetStructureGeneration();
    const bool textChanged = m_MatchedFilter != m_FilterText;
    if (!textChanged && !b_MatchesDirty && entityCount == m_MatchedEntityCount && generation == m_MatchedGeneration)
      return;

    if (textChanged)
      m_SearchCollapsed.clear();
    m_MatchedFilter = m_FilterText;
    m_MatchedEntityCount = entityCount;
    m_MatchedGeneration = generation;
    b_MatchesDirty = false;
    m_Matches.clear();
    m_FilterVisible.clear();

    for (auto [entity, name, hierarchy] : registry.view<Name, HierarchyComponent>().each())
    {
      if (registry.all_of<EditorOnlyTag>(entity) || !ContainsCaseInsensitive(name, m_MatchedFilter))
        continue;

      m_Matches.insert(entity);
      // Stops at the first ancestor already listed: a previous match put the rest of the chain in
      Entity ancestor = entity;
      while (ancestor != entt::null && m_FilterVisible.insert(ancestor).second)
        ancestor = registry.get<HierarchyComponent>(ancestor).parent;
    }
  }

  void OutlinerPanel::BuildRows(Scene& scene)
  {
    m_Rows.clear();
    for (Entity root : m_Roots)
      AppendRows(scene.GetRegistry(), root, 0);
  }

  void OutlinerPanel::AppendRows(entt::registry& registry, Entity entity, uint32_t depth)
  {
    const bool filtering = IsFiltering();
    if (filtering && !m_FilterVisible.contains(entity))
      return;
    if (registry.all_of<EditorOnlyTag>(entity))
      return;

    const HierarchyComponent& hierarchy = registry.get<HierarchyComponent>(entity);
    bool hasChildren = false;
    for (Entity child = hierarchy.firstChild; child != entt::null && !hasChildren;
      child = registry.get<HierarchyComponent>(child).nextSibling)
    {
      hasChildren = !filtering || m_FilterVisible.contains(child);
    }

    const bool open = hasChildren && (filtering ? !m_SearchCollapsed.contains(entity) : m_Expanded.contains(entity));
    m_Rows.push_back(Row { .entity = entity, .depth = depth, .hasChildren = hasChildren, .open = open });
    if (!open)
      return;

    for (Entity child = hierarchy.firstChild; child != entt::null; child = registry.get<HierarchyComponent>(child).nextSibling)
      AppendRows(registry, child, depth + 1);
  }

  bool OutlinerPanel::HandleKeyboardNavigation(EditorContext& context)
  {
    // The arrows belong to a field being edited, and to the viewport while the mouse is over it
    if (m_Rows.empty() || m_RenamingEntity != entt::null || context.viewportHovered
      || ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive()
      || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
      return false;
    }

    // Owned every frame the panel has focus, so ImGui's own keyboard navigation does not also walk the
    // row widgets: it only reads keys nobody owns, and ownership taken now applies from the next frame
    constexpr ImGuiKey KEYS[] = { ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_LeftArrow, ImGuiKey_RightArrow };
    const ImGuiID owner = ImGui::GetID("##KeyboardNavigation");
    for (ImGuiKey key : KEYS)
      ImGui::SetKeyOwner(key, owner);

    int32_t index = -1;
    for (size_t i = 0; i < m_Rows.size(); i++)
    {
      if (m_Rows[i].entity == context.selectedEntity)
      {
        index = int32_t(i);
        break;
      }
    }

    const int32_t last = int32_t(m_Rows.size()) - 1;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
    {
      SelectRow(context, size_t(index < 0 ? last : std::max(index - 1, 0)));
      return false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
    {
      SelectRow(context, size_t(index < 0 ? 0 : std::min(index + 1, last)));
      return false;
    }
    if (index < 0)
      return false;

    const Row& row = m_Rows[size_t(index)];
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
    {
      if (row.hasChildren && !row.open)
      {
        SetOpen(row.entity, true);
        return true;
      }
      // An open row lists its first child right below it
      if (row.open && index < last)
        SelectRow(context, size_t(index + 1));
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
    {
      if (row.open)
      {
        SetOpen(row.entity, false);
        return true;
      }
      for (int32_t parent = index - 1; parent >= 0 && row.depth > 0; parent--)
      {
        if (m_Rows[size_t(parent)].depth < row.depth)
        {
          SelectRow(context, size_t(parent));
          break;
        }
      }
    }
    return false;
  }

  void OutlinerPanel::SelectRow(EditorContext& context, size_t index)
  {
    const Entity entity = m_Rows[index].entity;
    if (entity == context.selectedEntity)
      return;

    m_LastSelection = entity;
    context.SelectEntity(entity);
    m_ScrollTarget = entity;
  }

  void OutlinerPanel::BeginRename(Scene& scene, Entity entity)
  {
    m_RenamingEntity = entity;
    const Name* name = scene.GetRegistry().try_get<Name>(entity);
    snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", name != nullptr ? name->c_str() : "");
    b_RenameNeedsFocus = true;
    m_ScrollTarget = entity;
  }

  void OutlinerPanel::FinishRename(Scene& scene, bool commit)
  {
    // The rename bumps the scene's structure generation, which redoes the filter pass
    entt::registry& registry = scene.GetRegistry();
    if (commit && registry.valid(m_RenamingEntity) && registry.all_of<Name>(m_RenamingEntity))
      EditorCommands::RenameEntity(scene, m_RenamingEntity, m_RenameBuffer);

    m_RenamingEntity = entt::null;
    b_RenameNeedsFocus = false;
  }

  void OutlinerPanel::FinishCreate(EditorContext& context, Entity entity)
  {
    if (entity == entt::null)
      return;

    // The new name rarely matches the search, and a row the filter hides cannot be renamed
    m_FilterText[0] = '\0';
    context.SelectEntity(entity);
    BeginRename(*context.scene, entity);
  }

  void OutlinerPanel::DrawCreateMenu(EditorContext& context)
  {
    if (!ImGui::BeginMenu("Create"))
      return;

    Scene& scene = *context.scene;
    AssetManager& assets = *context.assetManager;
    using Kind = EditorCommands::NewEntityKind;

    auto createItem = [&](const char* label, const char* icon, Kind kind) {
      if (ImGui::MenuItemEx(label, icon))
        FinishCreate(context, EditorCommands::CreateEntity(scene, assets, kind));
    };

    createItem("Empty Entity", ICON_LC_BOX, Kind::Empty);

    ImGui::Separator();
    createItem("Point Light", ICON_LC_LIGHTBULB, Kind::PointLight);
    createItem("Spot Light", ICON_LC_LIGHTBULB, Kind::SpotLight);
    createItem("Directional Light", ICON_LC_SUN, Kind::DirectionalLight);

    ImGui::Separator();
    createItem("Camera", ICON_LC_VIDEO, Kind::Camera);

    ImGui::Separator();
    createItem("Reflection Probe", ICON_LC_GLOBE, Kind::ReflectionProbe);
    createItem("Irradiance Volume", ICON_LC_BOXES, Kind::IrradianceVolume);

    ImGui::Separator();
    if (ImGui::BeginMenuEx("Primitives", ICON_LC_SHAPES))
    {
      struct PrimitiveEntry
      {
        const char* icon;
        PrimitiveType type;
      };
      constexpr PrimitiveEntry PRIMITIVES[] = {
        { .icon = ICON_LC_BOX, .type = PrimitiveType::Box },
        { .icon = ICON_LC_CIRCLE, .type = PrimitiveType::Sphere },
        { .icon = ICON_LC_SQUARE, .type = PrimitiveType::Plane },
      };

      for (const PrimitiveEntry& primitive : PRIMITIVES)
      {
        if (ImGui::MenuItemEx(EditorCommands::GetPrimitiveName(primitive.type), primitive.icon))
          FinishCreate(context, EditorCommands::CreatePrimitive(scene, assets, primitive.type));
      }

      ImGui::EndMenu();
    }
    createItem("Terrain", ICON_LC_MOUNTAIN, Kind::Terrain);

    ImGui::Separator();
    if (ImGui::MenuItemEx("Import Model...", ICON_LC_FILE_INPUT))
    {
      nfdu8filteritem_t filters[] = {
        { "3D Models", "gltf,glb,obj,fbx" },
      };
      std::string path = FileDialog::OpenFile(filters, 1);
      if (!path.empty())
      {
        Entity root = EditorCommands::ImportModel(assets, path);
        if (root != entt::null)
          context.SelectEntity(root);
      }
    }

    ImGui::EndMenu();
  }

  void OutlinerPanel::DrawEntityMenu(EditorContext& context, Entity entity)
  {
    Scene& scene = *context.scene;
    entt::registry& registry = scene.GetRegistry();

    if (ImGui::MenuItemEx("Rename", ICON_LC_PENCIL))
      BeginRename(scene, entity);

    const bool isModelInstance = registry.all_of<ModelSourceComponent>(entity);
    if ((isModelInstance || context.componentRegistry != nullptr) && ImGui::MenuItemEx("Duplicate", ICON_LC_COPY))
    {
      m_PendingAction = PendingAction::Duplicate;
      m_PendingEntity = entity;
    }

    if (IsInsideModel(scene, entity) && ImGui::MenuItemEx("Add Node", ICON_LC_SQUARE_PLUS))
    {
      m_PendingAction = PendingAction::AddNode;
      m_PendingEntity = entity;
    }

    if (registry.all_of<MeshComponent>(entity))
    {
      const bool visible = !registry.all_of<HiddenTag>(entity);
      if (ImGui::MenuItemEx(visible ? "Hide" : "Show", visible ? ICON_LC_EYE_OFF : ICON_LC_EYE))
        ToggleHidden(scene, entity);
    }

    ImGui::Separator();

    DrawCreateMenu(context);

    if (ImGui::BeginMenuEx("Add Component", ICON_LC_CIRCLE_PLUS))
    {
      EditorWidgets::AddComponentMenuItems(context, entity);
      ImGui::EndMenu();
    }

    ImGui::Separator();

    const ModelNodeComponent* node = registry.try_get<ModelNodeComponent>(entity);
    const bool isModelNode = node != nullptr && node->nodeIndex != 0;

    if (ImGui::MenuItemEx("Delete", ICON_LC_TRASH_2))
    {
      m_PendingAction = PendingAction::Delete;
      m_PendingEntity = entity;
    }

    if (isModelNode && ImGui::IsItemHovered())
      ImGui::SetTooltip("Recorded as a model override and reapplied on load");
  }

  void OutlinerPanel::ApplyPendingAction(EditorContext& context)
  {
    const PendingAction action = m_PendingAction;
    const Entity entity = m_PendingEntity;
    m_PendingAction = PendingAction::None;
    m_PendingEntity = entt::null;

    Scene& scene = *context.scene;
    if (action == PendingAction::None || !scene.GetRegistry().valid(entity))
      return;

    switch (action)
    {
      case PendingAction::Duplicate:
      {
        Entity copy = entt::null;
        if (scene.HasComponent<ModelSourceComponent>(entity))
        {
          if (context.assetManager != nullptr)
            copy = EditorCommands::DuplicateModel(scene, *context.assetManager, entity);
        }
        else if (context.componentRegistry != nullptr)
        {
          copy = scene.DuplicateEntity(entity, *context.componentRegistry);
        }

        if (copy != entt::null)
        {
          context.SelectEntity(copy);
          BeginRename(scene, copy);
        }
        break;
      }
      case PendingAction::AddNode:
      {
        Entity node = scene.CreateEntity(scene.MakeUniqueEntityName("Node"));
        scene.SetParent(node, entity);
        m_FilterText[0] = '\0';
        context.SelectEntity(node);
        BeginRename(scene, node);
        break;
      }
      case PendingAction::Delete:
        EditorCommands::DeleteEntity(context, entity);
        break;
      case PendingAction::None:
        break;
    }
  }

  void OutlinerPanel::DrawBackgroundInput(EditorContext& context)
  {
    if (ImGui::BeginPopupContextWindow("##OutlinerContextWindow", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight))
    {
      DrawCreateMenu(context);
      ImGui::EndPopup();
    }

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
    {
      m_LastSelection = entt::null;
      context.ClearSelection();
    }
  }

  void OutlinerPanel::DrawEmptyState(EditorContext& context)
  {
    FinishRename(*context.scene, false);
    m_ScrollTarget = entt::null;

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (IsFiltering())
      ImGui::TextWrapped("No entities match \"%s\"", m_FilterText);
    else
      ImGui::TextWrapped("The scene is empty. Right-click here to create an entity.");
    ImGui::PopStyleColor();

    DrawBackgroundInput(context);
  }

  void OutlinerPanel::DrawRows(EditorContext& context)
  {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float rowHeight = ImGui::GetFontSize() + style.FramePadding.y;

    int32_t scrollIndex = -1;
    int32_t renameIndex = -1;
    int32_t menuIndex = -1;
    for (size_t i = 0; i < m_Rows.size(); i++)
    {
      const Entity entity = m_Rows[i].entity;
      if (entity == m_ScrollTarget)
        scrollIndex = int32_t(i);
      if (entity == m_RenamingEntity)
        renameIndex = int32_t(i);
      if (entity == m_MenuEntity)
        menuIndex = int32_t(i);
    }

    if (scrollIndex < 0)
      m_ScrollTarget = entt::null;
    // Collapsed away or filtered out: the field is gone, so nothing would ever commit it
    if (renameIndex < 0 && m_RenamingEntity != entt::null)
      FinishRename(*context.scene, false);

    // Stays pushed until EndTable: the table reads the vertical cell padding again at every row
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x, 0.0f));
    if (!ImGui::BeginTable("Entities", 2, ImGuiTableFlags_ScrollY, ImVec2(0.0f, 0.0f)))
    {
      ImGui::PopStyleVar();
      return;
    }

    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Visibility", ImGuiTableColumnFlags_WidthFixed, rowHeight);

    ImGuiListClipper clipper;
    clipper.Begin(int32_t(m_Rows.size()));
    if (scrollIndex >= 0)
      clipper.IncludeItemByIndex(scrollIndex);
    if (renameIndex >= 0)
      clipper.IncludeItemByIndex(renameIndex);
    if (menuIndex >= 0)
      clipper.IncludeItemByIndex(menuIndex);

    m_MenuEntity = entt::null;
    while (clipper.Step())
    {
      for (int32_t i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
        DrawRow(context, m_Rows[size_t(i)], rowHeight);
    }

    DrawBackgroundInput(context);
    ImGui::EndTable();
    ImGui::PopStyleVar();
  }

  void OutlinerPanel::DrawRow(EditorContext& context, const Row& row, float rowHeight)
  {
    Scene& scene = *context.scene;
    entt::registry& registry = scene.GetRegistry();
    const Entity entity = row.entity;

    ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
    ImGui::TableSetColumnIndex(0);
    if (!registry.valid(entity))
      return;

    const EditorTheme& theme = EditorStyle::GetTheme();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float fontSize = ImGui::GetFontSize();
    // Tighter than a frame, so the rename field and the visibility button fit the row height
    const ImVec2 compactPadding(style.FramePadding.x, (rowHeight - fontSize) * 0.5f);

    // The entity scope keeps rows that share a name apart, while the plain name stays the label the
    // bridge matches: "Outliner/**/<name>". Icons are drawn beside it for the same reason.
    ImGui::PushID(int32_t(entt::to_integral(entity)));

    const ImVec2 cellStart = ImGui::GetCursorScreenPos();
    const float textY = cellStart.y + (rowHeight - fontSize) * 0.5f;
    const float chevronWidth = ImGui::CalcTextSize(ICON_LC_CHEVRON_RIGHT).x;
    const float chevronX = cellStart.x + float(row.depth) * style.IndentSpacing;
    const float iconX = chevronX + chevronWidth + style.ItemInnerSpacing.x;
    const float nameX = iconX + ImGui::CalcTextSize(ICON_LC_BOX).x + style.ItemInnerSpacing.x;

    char unnamed[32];
    const char* name = EditorCommands::GetEntityDisplayName(registry, entity, unnamed);
    const bool matched = IsFiltering() && m_Matches.contains(entity);

    // The name goes first so the chevron and the visibility button added after it take the hover
    // from its full-row hit box
    ImGui::SetCursorScreenPos(ImVec2(nameX, cellStart.y));
    if (entity == m_RenamingEntity)
    {
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, compactPadding);
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (b_RenameNeedsFocus)
        ImGui::SetKeyboardFocusHere();
      ImGui::PushID(ROW_WIDGET_SCOPE);
      const bool entered = ImGui::InputText("##Rename", m_RenameBuffer, sizeof(m_RenameBuffer),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
      ImGui::PopID();
      ImGui::PopStyleVar();

      if (entered)
        FinishRename(scene, true);
      else if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        FinishRename(scene, false);
      else if (b_RenameNeedsFocus)
        b_RenameNeedsFocus = !ImGui::IsItemActive();
      else if (!ImGui::IsItemActive() && !ImGui::IsItemFocused())
        FinishRename(scene, true);
    }
    else
    {
      // Ancestors listed only to reach a match stay dimmer than the matches themselves
      const bool dimmed = IsFiltering() && !matched;
      // ImGui hides a label from "##" on. Such a name keeps its full label, which is its id and bridge label,
      // but the Selectable draws it invisibly and the whole name is drawn over it.
      const bool literalName = std::strstr(name, "##") != nullptr;
      if (literalName)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
      else if (dimmed)
        ImGui::PushStyleColor(ImGuiCol_Text, ToImGuiColor(theme.textSecondary));
      ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
      const bool pressed = ImGui::Selectable(name, entity == context.selectedEntity,
        ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick,
        ImVec2(0.0f, rowHeight));
      ImGui::PopStyleVar();
      if (literalName || dimmed)
        ImGui::PopStyleColor();
      if (literalName)
        drawList->AddText(ImVec2(nameX, textY), ThemeColor(dimmed ? theme.textSecondary : theme.textPrimary), name);

      if (pressed)
      {
        m_LastSelection = entity;
        context.SelectEntity(entity);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
          BeginRename(scene, entity);
      }

      ImGui::PushID(ROW_WIDGET_SCOPE);
      if (ImGui::BeginPopupContextItem("##EntityMenu"))
      {
        m_MenuEntity = entity;
        DrawEntityMenu(context, entity);
        ImGui::EndPopup();
      }
      ImGui::PopID();

      const size_t matchOffset = matched ? FindCaseInsensitive(name, m_MatchedFilter) : std::string_view::npos;
      if (matchOffset != std::string_view::npos)
      {
        const char* matchBegin = name + matchOffset;
        const char* matchEnd = matchBegin + m_MatchedFilter.size();
        const ImVec2 matchPos(nameX + ImGui::CalcTextSize(name, matchBegin).x, textY);
        const float matchWidth = ImGui::CalcTextSize(matchBegin, matchEnd).x;
        const ImU32 highlight = ThemeColor(theme.accentHovered);
        drawList->AddText(matchPos, highlight, matchBegin, matchEnd);
        drawList->AddLine(ImVec2(matchPos.x, textY + fontSize), ImVec2(matchPos.x + matchWidth, textY + fontSize), highlight);
      }

      // Covers this node only - a collapsed parent does not report overrides in its subtree
      const bool overridden = context.componentRegistry != nullptr && context.assetManager != nullptr
        && ModelOverrides::IsNodeOverridden(scene, *context.assetManager, *context.componentRegistry, entity);
      if (overridden)
      {
        drawList->AddText(ImVec2(nameX + ImGui::CalcTextSize(name).x + style.ItemInnerSpacing.x, textY),
          ThemeColor(theme.warning), "*");
      }
    }

    ImGui::PushID(ROW_WIDGET_SCOPE);
    if (row.hasChildren)
    {
      ImGui::SetCursorScreenPos(ImVec2(chevronX, cellStart.y));
      if (ImGui::InvisibleButton("##Expand", ImVec2(chevronWidth + style.ItemInnerSpacing.x, rowHeight)))
        SetOpen(entity, !row.open);
      drawList->AddText(ImVec2(chevronX, textY), ThemeColor(ImGui::IsItemHovered() ? theme.textPrimary : theme.textSecondary),
        row.open ? ICON_LC_CHEVRON_DOWN : ICON_LC_CHEVRON_RIGHT);
    }

    if (const char* icon = GetEntityIcon(registry, entity))
      drawList->AddText(ImVec2(iconX, textY), ThemeColor(theme.textSecondary), icon);

    ImGui::TableSetColumnIndex(1);
    if (registry.all_of<MeshComponent>(entity))
    {
      const bool visible = !registry.all_of<HiddenTag>(entity);
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, compactPadding);
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
      ImGui::PushStyleColor(ImGuiCol_Text, ToImGuiColor(visible ? theme.textSecondary : theme.textDisabled));
      const bool toggled = EditorWidgets::InlineButton("Visibility", {
        .icon = visible ? ICON_LC_EYE : ICON_LC_EYE_OFF,
        .iconOnly = true,
        .tooltip = visible ? "Shown in the viewport. Click to hide the mesh." : "Hidden in the viewport. Click to show the mesh." });
      ImGui::PopStyleColor(2);
      ImGui::PopStyleVar();
      if (toggled)
        ToggleHidden(scene, entity);
    }
    ImGui::PopID();

    if (entity == m_ScrollTarget)
    {
      m_ScrollTarget = entt::null;
      const float windowTop = ImGui::GetWindowPos().y;
      if (cellStart.y < windowTop || cellStart.y + rowHeight > windowTop + ImGui::GetWindowHeight())
        ImGui::SetScrollFromPosY(cellStart.y + rowHeight * 0.5f - windowTop, 0.5f);
    }

    ImGui::PopID();
  }

  void OutlinerPanel::OnRender(EditorContext& context)
  {
    TakeRevealRequest(context);

    // A pending reveal survives a collapsed window: the request above already pulled the
    // panel forward, and the scroll target is only cleared once its row has been drawn
    if (!BeginPanel())
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

    Scene& scene = *context.scene;
    FollowSelection(context);

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##Search", ICON_LC_SEARCH " Search", m_FilterText, sizeof(m_FilterText));

    UpdateRootOrder(scene);
    UpdateFilter(scene);
    BuildRows(scene);
    if (HandleKeyboardNavigation(context))
      BuildRows(scene);

    if (m_Rows.empty())
      DrawEmptyState(context);
    else
      DrawRows(context);

    ApplyPendingAction(context);

    ImGui::End();
  }
}
