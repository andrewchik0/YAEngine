#pragma once

#include "Editor/IEditorPanel.h"
#include "Assets/Handle.h"

namespace YAEngine
{
  class MaterialBrowserPanel : public IEditorPanel
  {
  public:

    const char* GetName() const override { return "Materials"; }
    void OnRender(EditorContext& context) override;

  private:

    void BeginRename(MaterialHandle handle, const std::string& currentName);

    char m_FilterText[256] = {};
    MaterialHandle m_RenamingMaterial = MaterialHandle::Invalid();
    MaterialHandle m_PendingDelete = MaterialHandle::Invalid();
    char m_RenameBuffer[256] = {};
    bool b_RenameNeedsFocus = false;
  };
}
