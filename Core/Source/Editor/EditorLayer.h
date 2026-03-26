#pragma once

#include "Layer.h"
#include "Editor/EditorContext.h"
#include "Editor/IEditorPanel.h"

namespace YAEngine
{
  class EditorLayer : public Layer
  {
  public:

    void OnAttach() override;
    void OnSceneReady() override;
    void RenderUI() override;
    void OnDetach() override;

  private:

    void BuildDefaultLayout(uint32_t dockspaceId);

    EditorContext m_Context;
    std::vector<std::unique_ptr<IEditorPanel>> m_Panels;
    bool m_LayoutBuilt = false;
  };
}
