#pragma once

#include "Editor/IEditorPanel.h"
#include "Scene/Scene.h"

namespace YAEngine
{
  class OutlinerPanel : public IEditorPanel
  {
  public:

    static constexpr EditorPanelDescriptor DESCRIPTOR { .name = "Outliner", .category = EditorPanelCategory::Scene };

    const EditorPanelDescriptor& GetDescriptor() const override { return DESCRIPTOR; }
    void OnRender(EditorContext& context) override;
    void OnSceneReady(EditorContext& context) override;

  private:

    struct Row
    {
      Entity entity { entt::null };
      uint32_t depth = 0;
      bool hasChildren = false;
      bool open = false;
    };

    // Context menu choices that add or destroy entities, applied once every row has been drawn
    enum class PendingAction : uint8_t { None, Duplicate, AddNode, Delete };

    bool IsFiltering() const { return m_FilterText[0] != '\0'; }

    void TakeRevealRequest(EditorContext& context);
    void FollowSelection(EditorContext& context);
    void Reveal(Scene& scene, Entity entity);
    void SetOpen(Entity entity, bool open);

    void UpdateRootOrder(Scene& scene);
    void UpdateFilter(Scene& scene);
    void BuildRows(Scene& scene);
    void AppendRows(entt::registry& registry, Entity entity, uint32_t depth);

    // While the panel has focus and no text field is active: Up and Down move the selection through the
    // listed rows, Right expands a row or steps into its first child, Left collapses it or steps out to
    // its parent. Returns whether a row was expanded or collapsed.
    bool HandleKeyboardNavigation(EditorContext& context);
    void SelectRow(EditorContext& context, size_t index);

    void DrawRows(EditorContext& context);
    void DrawRow(EditorContext& context, const Row& row, float rowHeight);
    void DrawEmptyState(EditorContext& context);
    void DrawBackgroundInput(EditorContext& context);
    void DrawEntityMenu(EditorContext& context, Entity entity);
    void DrawCreateMenu(EditorContext& context);
    void FinishCreate(EditorContext& context, Entity entity);
    void ApplyPendingAction(EditorContext& context);

    void BeginRename(Scene& scene, Entity entity);
    void FinishRename(Scene& scene, bool commit);

    char m_FilterText[256] = {};

    // Result of the last filter pass, redone only when the text, the entity count or the scene's structure
    // generation changes - never per drawn row
    std::string m_MatchedFilter;
    size_t m_MatchedEntityCount = 0;
    uint64_t m_MatchedGeneration = 0;
    bool b_MatchesDirty = true;
    std::unordered_set<Entity> m_Matches;
    // Matches and every ancestor of one
    std::unordered_set<Entity> m_FilterVisible;
    // Ancestors of matches folded during the current search; a search opens all others
    std::unordered_set<Entity> m_SearchCollapsed;
    std::unordered_set<Entity> m_Expanded;

    // A root entity's place is fixed the first time it is seen, so the top level keeps creation
    // order while entt storage order shuffles on every deletion
    std::unordered_map<Entity, uint64_t> m_RootOrder;
    uint64_t m_NextRootOrder = 0;
    // Sorted by m_RootOrder, again only when the set of roots changes
    std::vector<Entity> m_Roots;
    // Sum of the hashed ids of m_Roots, which tells a changed set of roots from the same one
    uint64_t m_RootSignature = 0;
    // This frame's roots before the comparison with m_Roots
    std::vector<Entity> m_RootScratch;
    // Rebuilt every frame from the expanded nodes; only the clipped range is drawn
    std::vector<Row> m_Rows;

    Entity m_RenamingEntity = entt::null;
    char m_RenameBuffer[256] = {};
    bool b_RenameNeedsFocus = false;

    // Selection as of the last draw; a change made elsewhere (viewport, bridge) is revealed
    Entity m_LastSelection = entt::null;
    // Row scrolled into view when it is drawn next
    Entity m_ScrollTarget = entt::null;
    // Row whose context menu is open, kept drawn while it is scrolled out of the clipped range
    Entity m_MenuEntity = entt::null;

    PendingAction m_PendingAction = PendingAction::None;
    Entity m_PendingEntity = entt::null;
  };
}
