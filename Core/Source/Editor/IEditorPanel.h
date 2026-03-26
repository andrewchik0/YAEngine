#pragma once

namespace YAEngine
{
  struct EditorContext;

  class IEditorPanel
  {
  public:
    virtual ~IEditorPanel() = default;

    virtual const char* GetName() const = 0;
    virtual void OnRender(EditorContext& context) = 0;
    virtual void OnSceneReady(EditorContext& context) {}

    bool IsVisible() const { return m_Visible; }
    void SetVisible(bool visible) { m_Visible = visible; }

  protected:
    bool m_Visible = true;
  };
}
