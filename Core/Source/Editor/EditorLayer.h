#pragma once

#include "Layer.h"
#include "Editor/EditorContext.h"
#include "Editor/IEditorPanel.h"
#include "Editor/Utils/EditorTextureCache.h"

namespace YAEngine
{
  class EditorLayer : public Layer
  {
  public:

    void OnAttach() override;
    void OnSceneReady() override;
    void Update(double deltaTime) override;
    void RenderUI() override;
    void OnDetach() override;

  private:

    void BuildDefaultLayout(uint32_t dockspaceId);

    EditorContext m_Context;
    EditorTextureCache m_TextureCache;
    std::vector<std::unique_ptr<IEditorPanel>> m_Panels;
    bool b_LayoutBuilt = false;
    bool b_ResetLayout = false;
    uint32_t m_LastViewportWidth = 0;
    uint32_t m_LastViewportHeight = 0;
  };
}
