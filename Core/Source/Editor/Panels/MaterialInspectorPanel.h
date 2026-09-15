#pragma once

#include "Editor/IEditorPanel.h"
#include "Editor/EditorCommands.h"

namespace YAEngine
{
  class MaterialInspectorPanel : public IEditorPanel
  {
  public:

    static constexpr EditorPanelDescriptor DESCRIPTOR { .name = "Material Inspector", .category = EditorPanelCategory::Assets };

    const EditorPanelDescriptor& GetDescriptor() const override { return DESCRIPTOR; }
    void OnRender(EditorContext& context) override;

  private:
    EditorCommands::MaterialUserCount m_Users;
  };
}
