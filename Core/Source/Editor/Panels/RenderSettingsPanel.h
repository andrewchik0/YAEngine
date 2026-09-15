#pragma once

#include "Editor/IEditorPanel.h"

namespace YAEngine
{
  class RenderSettingsPanel : public IEditorPanel
  {
  public:

    static constexpr EditorPanelDescriptor DESCRIPTOR { .name = "Render Settings", .category = EditorPanelCategory::Rendering };

    const EditorPanelDescriptor& GetDescriptor() const override { return DESCRIPTOR; }
    void OnRender(EditorContext& context) override;
  };
}
