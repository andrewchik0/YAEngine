#pragma once

#include "Editor/IEditorPanel.h"

namespace YAEngine
{
  class RenderSettingsPanel : public IEditorPanel
  {
  public:

    const char* GetName() const override { return "Render Settings"; }
    void OnRender(EditorContext& context) override;
  };
}
