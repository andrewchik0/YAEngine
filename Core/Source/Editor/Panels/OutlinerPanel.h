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
    void BeginRename(EditorContext& context, Entity entity);

    char m_FilterText[256] = {};
    Entity m_RenamingEntity = entt::null;
    char m_RenameBuffer[256] = {};
    bool b_RenameNeedsFocus = false;
  };
}
