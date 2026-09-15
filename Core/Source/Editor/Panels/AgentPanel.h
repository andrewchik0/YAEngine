#pragma once

#include "Editor/IEditorPanel.h"

namespace YAEngine
{
  class EditorBridge;
  struct EditorPreferences;

  class AgentPanel : public IEditorPanel
  {
  public:
    static constexpr EditorPanelDescriptor DESCRIPTOR { .name = "AI Agent", .category = EditorPanelCategory::Tools };

    // mcpOverride: --mcp or --no-mcp of this run, which won over the saved preference at startup
    AgentPanel(EditorBridge& bridge, EditorPreferences& preferences, std::optional<bool> mcpOverride);

    const EditorPanelDescriptor& GetDescriptor() const override { return DESCRIPTOR; }
    void OnRender(EditorContext& context) override;

  private:
    void DrawConnection();
    void DrawClients();
    void DrawActivity();

    EditorBridge& m_Bridge;
    EditorPreferences& m_Preferences;
    std::optional<bool> m_McpOverride;
  };
}
