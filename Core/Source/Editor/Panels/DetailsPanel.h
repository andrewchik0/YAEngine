#pragma once

#include "Editor/IEditorPanel.h"

namespace YAEngine
{
  class DetailsPanel : public IEditorPanel
  {
  public:

    const char* GetName() const override { return "Details"; }
    void OnRender(EditorContext& context) override;
  };
}
