#pragma once

#include "Editor/IEditorPanel.h"

namespace YAEngine
{
  class MaterialInspectorPanel : public IEditorPanel
  {
  public:

    const char* GetName() const override { return "Material Inspector"; }
    void OnRender(EditorContext& context) override;
  };
}
