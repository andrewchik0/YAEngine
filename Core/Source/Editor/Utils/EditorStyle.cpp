#include "EditorStyle.h"

#include <implot.h>

namespace YAEngine
{
  namespace
  {
    EditorTheme s_Theme;
    float s_ContentScale = 1.0f;

    constexpr ImVec4 INVISIBLE(0.0f, 0.0f, 0.0f, 0.0f);

    ImVec4 WithAlpha(const glm::vec4& color, float alpha)
    {
      ImVec4 result = ToImGuiColor(color);
      result.w = alpha;
      return result;
    }

    ImVec4 Mix(const glm::vec4& from, const glm::vec4& to, float t)
    {
      return ToImGuiColor(glm::mix(from, to, t));
    }

    ImVec2 Scaled(const ImVec2& value, float scale)
    {
      return ImVec2(value.x * scale, value.y * scale);
    }
  }

  const EditorTheme& EditorStyle::GetTheme()
  {
    return s_Theme;
  }

  float EditorStyle::GetContentScale()
  {
    return s_ContentScale;
  }

  void EditorStyle::Apply(const EditorTheme& theme, float contentScale)
  {
    s_Theme = theme;
    s_ContentScale = contentScale > 0.0f ? contentScale : 1.0f;
    const EditorTheme& t = s_Theme;

    // Built from scratch on every apply so repeated scale changes never compound
    ImGuiStyle next;
    ImGui::StyleColorsDark(&next);

    next.WindowPadding = ImVec2(8.0f, 8.0f);
    next.FramePadding = ImVec2(t.framePadding.x, t.framePadding.y);
    next.ItemSpacing = ImVec2(t.itemSpacing.x, t.itemSpacing.y);
    next.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    next.CellPadding = ImVec2(6.0f, 3.0f);
    next.IndentSpacing = t.indent;
    next.ScrollbarSize = t.scrollbarSize;
    next.GrabMinSize = 10.0f;

    next.WindowRounding = t.radiusMedium;
    next.PopupRounding = t.radiusMedium;
    next.ScrollbarRounding = t.radiusMedium;
    next.ChildRounding = t.radiusSmall;
    next.FrameRounding = t.radiusSmall;
    next.GrabRounding = t.radiusSmall;
    next.TabRounding = t.radiusSmall;

    next.WindowBorderSize = 1.0f;
    next.ChildBorderSize = 1.0f;
    next.PopupBorderSize = 1.0f;
    next.FrameBorderSize = 0.0f;
    next.TabBorderSize = 0.0f;
    next.TabBarBorderSize = 1.0f;
    next.TabBarOverlineSize = 2.0f;
    // Panels close from their own tabs; the node-wide button would close every tab in the node
    next.DockingNodeHasCloseButton = false;

    next.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesToNodes;
    next.TreeLinesSize = 1.0f;

    next.ScaleAllSizes(s_ContentScale);

    // Fonts follow the content scale through FontScaleDpi rather than through their size
    next.FontSizeBase = t.fontSizeBody;
    next.FontScaleMain = ImGui::GetStyle().FontScaleMain;
    next.FontScaleDpi = s_ContentScale;

    ImVec4* c = next.Colors;
    c[ImGuiCol_Text] = ToImGuiColor(t.textPrimary);
    c[ImGuiCol_TextDisabled] = ToImGuiColor(t.textDisabled);
    c[ImGuiCol_WindowBg] = ToImGuiColor(t.panel);
    c[ImGuiCol_ChildBg] = INVISIBLE;
    c[ImGuiCol_PopupBg] = ToImGuiColor(t.popup);
    c[ImGuiCol_Border] = ToImGuiColor(t.borderSubtle);
    c[ImGuiCol_BorderShadow] = INVISIBLE;
    c[ImGuiCol_FrameBg] = ToImGuiColor(t.frame);
    c[ImGuiCol_FrameBgHovered] = ToImGuiColor(t.frameHovered);
    c[ImGuiCol_FrameBgActive] = ToImGuiColor(t.frameActive);
    c[ImGuiCol_TitleBg] = ToImGuiColor(t.appBackground);
    c[ImGuiCol_TitleBgActive] = ToImGuiColor(t.appBackground);
    c[ImGuiCol_TitleBgCollapsed] = ToImGuiColor(t.appBackground);
    c[ImGuiCol_MenuBarBg] = ToImGuiColor(t.appBackground);
    c[ImGuiCol_ScrollbarBg] = INVISIBLE;
    c[ImGuiCol_ScrollbarGrab] = ToImGuiColor(t.frameHovered);
    c[ImGuiCol_ScrollbarGrabHovered] = ToImGuiColor(t.borderStrong);
    c[ImGuiCol_ScrollbarGrabActive] = ToImGuiColor(t.textDisabled);
    c[ImGuiCol_CheckMark] = ToImGuiColor(t.accent);
    c[ImGuiCol_CheckboxSelectedBg] = ToImGuiColor(t.frame);
    c[ImGuiCol_SliderGrab] = ToImGuiColor(t.accent);
    c[ImGuiCol_SliderGrabActive] = ToImGuiColor(t.accentHovered);
    c[ImGuiCol_Button] = ToImGuiColor(t.frame);
    c[ImGuiCol_ButtonHovered] = ToImGuiColor(t.frameHovered);
    c[ImGuiCol_ButtonActive] = ToImGuiColor(t.frameActive);
    // Header colors double as the selection of selectables, tree nodes and menu items
    c[ImGuiCol_Header] = ToImGuiColor(t.accentMuted);
    c[ImGuiCol_HeaderHovered] = Mix(t.accentMuted, t.accent, 0.25f);
    c[ImGuiCol_HeaderActive] = Mix(t.accentMuted, t.accent, 0.45f);
    c[ImGuiCol_Separator] = ToImGuiColor(t.borderSubtle);
    c[ImGuiCol_SeparatorHovered] = ToImGuiColor(t.accentHovered);
    c[ImGuiCol_SeparatorActive] = ToImGuiColor(t.accent);
    c[ImGuiCol_ResizeGrip] = INVISIBLE;
    c[ImGuiCol_ResizeGripHovered] = ToImGuiColor(t.accentMuted);
    c[ImGuiCol_ResizeGripActive] = ToImGuiColor(t.accent);
    c[ImGuiCol_InputTextCursor] = ToImGuiColor(t.textPrimary);
    c[ImGuiCol_TabHovered] = ToImGuiColor(t.frameHovered);
    c[ImGuiCol_Tab] = ToImGuiColor(t.appBackground);
    c[ImGuiCol_TabSelected] = ToImGuiColor(t.panel);
    c[ImGuiCol_TabSelectedOverline] = ToImGuiColor(t.accent);
    c[ImGuiCol_TabDimmed] = ToImGuiColor(t.appBackground);
    c[ImGuiCol_TabDimmedSelected] = ToImGuiColor(t.panel);
    c[ImGuiCol_TabDimmedSelectedOverline] = INVISIBLE;
    c[ImGuiCol_DockingPreview] = WithAlpha(t.accent, 0.35f);
    c[ImGuiCol_DockingEmptyBg] = ToImGuiColor(t.appBackground);
    c[ImGuiCol_PlotLines] = ToImGuiColor(t.accent);
    c[ImGuiCol_PlotLinesHovered] = ToImGuiColor(t.accentHovered);
    c[ImGuiCol_PlotHistogram] = ToImGuiColor(t.accent);
    c[ImGuiCol_PlotHistogramHovered] = ToImGuiColor(t.accentHovered);
    c[ImGuiCol_TableHeaderBg] = ToImGuiColor(t.frame);
    c[ImGuiCol_TableBorderStrong] = ToImGuiColor(t.borderSubtle);
    c[ImGuiCol_TableBorderLight] = Mix(t.panel, t.borderSubtle, 0.6f);
    c[ImGuiCol_TableRowBg] = INVISIBLE;
    c[ImGuiCol_TableRowBgAlt] = WithAlpha(t.textPrimary, 0.03f);
    c[ImGuiCol_TextLink] = ToImGuiColor(t.accentHovered);
    c[ImGuiCol_TextSelectedBg] = WithAlpha(t.accent, 0.35f);
    c[ImGuiCol_TreeLines] = ToImGuiColor(t.borderStrong);
    c[ImGuiCol_DragDropTarget] = ToImGuiColor(t.accent);
    c[ImGuiCol_DragDropTargetBg] = WithAlpha(t.accent, 0.15f);
    c[ImGuiCol_UnsavedMarker] = ToImGuiColor(t.textPrimary);
    c[ImGuiCol_NavCursor] = ToImGuiColor(t.accent);
    c[ImGuiCol_NavWindowingHighlight] = WithAlpha(t.textPrimary, 0.7f);
    c[ImGuiCol_NavWindowingDimBg] = WithAlpha(t.appBackground, 0.6f);
    c[ImGuiCol_ModalWindowDimBg] = WithAlpha(t.appBackground, 0.6f);

    ImGui::GetStyle() = next;
    ApplyPlotStyle();
  }

  void EditorStyle::ApplyPlotStyle()
  {
    if (ImPlot::GetCurrentContext() == nullptr)
      return;

    const EditorTheme& t = s_Theme;
    const float scale = s_ContentScale;
    const ImPlotStyle defaults;
    ImPlotStyle& style = ImPlot::GetStyle();

    style.PlotDefaultSize = Scaled(defaults.PlotDefaultSize, scale);
    style.PlotMinSize = Scaled(defaults.PlotMinSize, scale);
    style.MajorTickLen = Scaled(defaults.MajorTickLen, scale);
    style.MinorTickLen = Scaled(defaults.MinorTickLen, scale);
    style.PlotPadding = Scaled(defaults.PlotPadding, scale);
    style.LabelPadding = Scaled(defaults.LabelPadding, scale);
    style.LegendPadding = Scaled(defaults.LegendPadding, scale);
    style.LegendInnerPadding = Scaled(defaults.LegendInnerPadding, scale);
    style.LegendSpacing = Scaled(defaults.LegendSpacing, scale);
    style.MousePosPadding = Scaled(defaults.MousePosPadding, scale);
    style.AnnotationPadding = Scaled(defaults.AnnotationPadding, scale);
    style.DigitalPadding = defaults.DigitalPadding * scale;
    style.DigitalSpacing = defaults.DigitalSpacing * scale;

    ImVec4* c = style.Colors;
    c[ImPlotCol_FrameBg] = INVISIBLE;
    c[ImPlotCol_PlotBg] = ToImGuiColor(t.appBackground);
    c[ImPlotCol_PlotBorder] = ToImGuiColor(t.borderSubtle);
    c[ImPlotCol_LegendBg] = ToImGuiColor(t.popup);
    c[ImPlotCol_LegendBorder] = ToImGuiColor(t.borderSubtle);
    c[ImPlotCol_LegendText] = ToImGuiColor(t.textPrimary);
    c[ImPlotCol_TitleText] = ToImGuiColor(t.textPrimary);
    c[ImPlotCol_InlayText] = ToImGuiColor(t.textPrimary);
    c[ImPlotCol_AxisText] = ToImGuiColor(t.textSecondary);
    c[ImPlotCol_AxisGrid] = WithAlpha(t.borderSubtle, 0.6f);
    c[ImPlotCol_AxisTick] = ToImGuiColor(t.borderStrong);
    c[ImPlotCol_AxisBg] = INVISIBLE;
    c[ImPlotCol_AxisBgHovered] = ToImGuiColor(t.frameHovered);
    c[ImPlotCol_AxisBgActive] = ToImGuiColor(t.frameActive);
    c[ImPlotCol_Selection] = ToImGuiColor(t.accent);
    c[ImPlotCol_Crosshairs] = ToImGuiColor(t.textSecondary);
  }
}
