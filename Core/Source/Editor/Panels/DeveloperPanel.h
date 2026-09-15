#pragma once

#include "Editor/IEditorPanel.h"
#include "Editor/Utils/EditorTheme.h"

namespace YAEngine
{
  struct EditorPreferences;

  // Live developer knobs and diagnostics, hidden by default. GROUPS (DeveloperPanel.cpp) lists the
  // groups in display order; adding one takes a draw method and an entry there.
  class DeveloperPanel : public IEditorPanel
  {
  public:
    static constexpr EditorPanelDescriptor DESCRIPTOR {
      .name = "Developer", .category = EditorPanelCategory::Tools, .defaultVisible = false, .developer = true };

    explicit DeveloperPanel(EditorPreferences& preferences);

    const EditorPanelDescriptor& GetDescriptor() const override { return DESCRIPTOR; }
    void OnRender(EditorContext& context) override;

    // ImGui's style must not change inside a frame, so theme edits wait for this call between frames
    void ApplyPendingTheme();

  private:
    struct Group
    {
      const char* label = nullptr;
      const char* icon = nullptr;
      const char* tooltip = nullptr;
      void (DeveloperPanel::*draw)(EditorContext& context) = nullptr;
    };

    static const Group GROUPS[];

    void DrawGtaoGroup(EditorContext& context);
    void DrawTaaGroup(EditorContext& context);
    void DrawPathTracerGroup(EditorContext& context);
    void DrawRayReconstructionGroup(EditorContext& context);
    void DrawIrradianceVolumeDiagnosticsGroup(EditorContext& context);
    void DrawReflectionProbeDiagnosticsGroup(EditorContext& context);
    void DrawThemeGroup(EditorContext& context);
    void DrawImGuiToolsGroup(EditorContext& context);
    void DrawImGuiToolWindows();

    EditorPreferences& m_Preferences;
    // Edited this frame, applied by ApplyPendingTheme
    std::optional<EditorTheme> m_PendingTheme;
    // Theme token row labels in VisitEditorThemeTokens order, built on first use
    std::vector<std::string> m_TokenLabels;
    bool b_ShowMetrics = false;
    bool b_ShowStyleEditor = false;
  };
}
