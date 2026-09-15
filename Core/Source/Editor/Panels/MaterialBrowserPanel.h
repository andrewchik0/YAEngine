#pragma once

#include "Editor/IEditorPanel.h"
#include "Assets/Handle.h"

namespace YAEngine
{
  class MaterialManager;

  class MaterialBrowserPanel : public IEditorPanel
  {
  public:

    static constexpr EditorPanelDescriptor DESCRIPTOR { .name = "Materials", .category = EditorPanelCategory::Assets };

    const EditorPanelDescriptor& GetDescriptor() const override { return DESCRIPTOR; }
    void OnRender(EditorContext& context) override;

  private:

    struct Entry
    {
      MaterialHandle handle;
      std::string name;
    };

    void RefreshEntries(MaterialManager& materials);
    void DrawList(EditorContext& context, MaterialManager& materials);
    void DrawRow(EditorContext& context, MaterialManager& materials, const Entry& entry, float rowHeight);
    void DrawBackgroundInput(EditorContext& context, MaterialManager& materials);
    void CreateMaterial(EditorContext& context, MaterialManager& materials);
    void BeginRename(MaterialHandle handle, const std::string& currentName);
    void FinishRename(MaterialManager& materials, bool commit);
    void ApplyPendingDelete(EditorContext& context, MaterialManager& materials);

    char m_FilterText[256] = {};

    // Every material sorted by name, rebuilt only when one is added, removed or renamed
    std::vector<Entry> m_Entries;
    // Set by this panel's own renames; renames elsewhere are found by the periodic name check
    bool b_EntriesDirty = true;
    // ImGui time of the last name check
    double m_EntriesCheckedAt = 0.0;
    // Indices into m_Entries passing m_VisibleFilter
    std::vector<uint32_t> m_Visible;
    std::string m_VisibleFilter;
    bool b_VisibleDirty = true;

    MaterialHandle m_RenamingMaterial = MaterialHandle::Invalid();
    MaterialHandle m_PendingDelete = MaterialHandle::Invalid();
    // Row scrolled into view when it is drawn next
    MaterialHandle m_ScrollTarget = MaterialHandle::Invalid();
    char m_RenameBuffer[256] = {};
    bool b_RenameNeedsFocus = false;
  };
}
