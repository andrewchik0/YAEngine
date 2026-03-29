#pragma once

#include "Editor/IEditorPanel.h"
#include "Scene/Scene.h"

namespace YAEngine
{
  class OutlinerPanel : public IEditorPanel
  {
  public:

    const char* GetName() const override { return "Outliner"; }
    void OnRender(EditorContext& context) override;

  private:

    void DrawEntity(EditorContext& context, Entity entity);
    void DrawCreateMenu(EditorContext& context);
    bool MatchesFilter(EditorContext& context, Entity entity);

    char m_FilterText[256] = {};
  };
}
