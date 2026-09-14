#pragma once

#include <imgui.h>

#include "Editor/IEditorPanel.h"

namespace YAEngine
{
  class EditorBridge;
  struct EditorPreferences;

  // A live listener in the panel, and connected clients in the main menu bar indicator
  inline constexpr ImVec4 AGENT_ACTIVE_COLOR { 0.4f, 0.85f, 0.45f, 1.0f };

  class AgentPanel : public IEditorPanel
  {
  public:
    AgentPanel(EditorBridge& bridge, EditorPreferences& preferences);

    const char* GetName() const override { return "AI Agent"; }
    void OnRender(EditorContext& context) override;

  private:
    void DrawStatus();
    void DrawClients();
    void DrawActivity();

    EditorBridge& m_Bridge;
    EditorPreferences& m_Preferences;
  };
}
