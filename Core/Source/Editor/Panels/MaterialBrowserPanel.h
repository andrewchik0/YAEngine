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

    char m_FilterText[256] = {};
  };
}
