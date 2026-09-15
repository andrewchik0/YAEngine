#pragma once

#include "Pch.h"

#include <imgui.h>

namespace YAEngine
{
  struct EditorContext;

  // View menu sections, in menu order
  enum class EditorPanelCategory : uint8_t
  {
    Scene,
    Assets,
    Rendering,
    Animation,
    Tools,
    Count
  };

  struct EditorPanelDescriptor
  {
    // Window name: the ImGui window id, the dock layout and preferences key, and the first
    // segment of bridge ui paths
    const char* name = nullptr;
    EditorPanelCategory category = EditorPanelCategory::Tools;
    bool defaultVisible = true;
    // Listed after the regular panels in the View menu
    bool developer = false;
  };

  class IEditorPanel
  {
  public:
    virtual ~IEditorPanel() = default;

    virtual const EditorPanelDescriptor& GetDescriptor() const = 0;
    virtual void OnRender(EditorContext& context) = 0;
    virtual void OnSceneReady(EditorContext& context) {}

    const char* GetName() const { return GetDescriptor().name; }
    bool IsVisible() const { return b_Visible; }
    void SetVisible(bool visible) { b_Visible = visible; }
    // Brings the panel to the front the next time it is drawn. Panels never take focus on their
    // own, not even when they appear, so this is reserved for explicit user actions.
    void RequestFocus() { b_FocusRequested = true; }

  protected:
    // ImGui::Begin for the panel's window, with the close button bound to its visibility
    bool BeginPanel(ImGuiWindowFlags flags = 0)
    {
      if (b_FocusRequested)
      {
        ImGui::SetNextWindowFocus();
        b_FocusRequested = false;
      }
      return ImGui::Begin(GetName(), &b_Visible, flags | ImGuiWindowFlags_NoFocusOnAppearing);
    }

  private:
    bool b_Visible = true;
    bool b_FocusRequested = false;
  };
}
