#pragma once

#include "Pch.h"

namespace YAEngine
{
  inline glm::vec4 EditorThemeColor(uint32_t rgb, uint8_t alpha = 0xFF)
  {
    return glm::vec4(
      float((rgb >> 16) & 0xFF) / 255.0f,
      float((rgb >> 8) & 0xFF) / 255.0f,
      float(rgb & 0xFF) / 255.0f,
      float(alpha) / 255.0f);
  }

  // The look of the editor as plain data, independent of ImGui. EditorStyle maps it onto
  // ImGuiStyle and ImPlot and multiplies sizes by the window content scale.
  struct EditorTheme
  {
    glm::vec4 appBackground = EditorThemeColor(0x1E1F22);
    glm::vec4 panel = EditorThemeColor(0x2B2D30);
    glm::vec4 frame = EditorThemeColor(0x393B40);
    glm::vec4 frameHovered = EditorThemeColor(0x43454A);
    glm::vec4 frameActive = EditorThemeColor(0x4E5157);
    // Strips drawn over the viewport image; the alpha is part of the token
    glm::vec4 overlay = EditorThemeColor(0x2B2D30, 0xBF);
    glm::vec4 popup = EditorThemeColor(0x2B2D30);

    glm::vec4 borderSubtle = EditorThemeColor(0x43454A);
    glm::vec4 borderStrong = EditorThemeColor(0x5A5D63);

    glm::vec4 textPrimary = EditorThemeColor(0xDFE1E5);
    glm::vec4 textSecondary = EditorThemeColor(0x9DA0A8);
    glm::vec4 textDisabled = EditorThemeColor(0x6F737A);

    glm::vec4 accent = EditorThemeColor(0x4C8DFF);
    glm::vec4 accentHovered = EditorThemeColor(0x6BA0FF);
    glm::vec4 accentActive = EditorThemeColor(0x3A78E8);
    // Selection backgrounds
    glm::vec4 accentMuted = EditorThemeColor(0x2E436E);

    glm::vec4 success = EditorThemeColor(0x5FB865);
    glm::vec4 warning = EditorThemeColor(0xE5A84B);
    glm::vec4 error = EditorThemeColor(0xE5534B);
    glm::vec4 info = EditorThemeColor(0x6BA0FF);

    glm::vec4 axisX = EditorThemeColor(0xE5534B);
    glm::vec4 axisY = EditorThemeColor(0x6CC24A);
    glm::vec4 axisZ = EditorThemeColor(0x4C8DFF);

    float radiusSmall = 4.0f;
    float radiusMedium = 6.0f;
    glm::vec2 framePadding = { 8.0f, 5.0f };
    glm::vec2 itemSpacing = { 8.0f, 6.0f };
    float indent = 18.0f;
    float scrollbarSize = 12.0f;
    // Share of a property grid's width taken by the label column
    float labelColumnFraction = 0.4f;

    // ImGui font sizes: the ascender to descender height, about 1.2 times Inter's em size
    float fontSizeBody = 16.0f;
    float fontSizeSmall = 14.0f;
    float fontSizeHeading = 18.0f;

    bool operator==(const EditorTheme&) const = default;
  };

  // Every token with its preferences key, as a member pointer so two themes can be compared.
  // Persistence and the theme editor walk this one list.
  template<typename Visitor>
  void VisitEditorThemeTokens(Visitor&& visit)
  {
    visit("app_background", &EditorTheme::appBackground);
    visit("panel", &EditorTheme::panel);
    visit("frame", &EditorTheme::frame);
    visit("frame_hovered", &EditorTheme::frameHovered);
    visit("frame_active", &EditorTheme::frameActive);
    visit("overlay", &EditorTheme::overlay);
    visit("popup", &EditorTheme::popup);
    visit("border_subtle", &EditorTheme::borderSubtle);
    visit("border_strong", &EditorTheme::borderStrong);
    visit("text_primary", &EditorTheme::textPrimary);
    visit("text_secondary", &EditorTheme::textSecondary);
    visit("text_disabled", &EditorTheme::textDisabled);
    visit("accent", &EditorTheme::accent);
    visit("accent_hovered", &EditorTheme::accentHovered);
    visit("accent_active", &EditorTheme::accentActive);
    visit("accent_muted", &EditorTheme::accentMuted);
    visit("success", &EditorTheme::success);
    visit("warning", &EditorTheme::warning);
    visit("error", &EditorTheme::error);
    visit("info", &EditorTheme::info);
    visit("axis_x", &EditorTheme::axisX);
    visit("axis_y", &EditorTheme::axisY);
    visit("axis_z", &EditorTheme::axisZ);
    visit("radius_small", &EditorTheme::radiusSmall);
    visit("radius_medium", &EditorTheme::radiusMedium);
    visit("frame_padding", &EditorTheme::framePadding);
    visit("item_spacing", &EditorTheme::itemSpacing);
    visit("indent", &EditorTheme::indent);
    visit("scrollbar_size", &EditorTheme::scrollbarSize);
    visit("label_column_fraction", &EditorTheme::labelColumnFraction);
    visit("font_size_body", &EditorTheme::fontSizeBody);
    visit("font_size_small", &EditorTheme::fontSizeSmall);
    visit("font_size_heading", &EditorTheme::fontSizeHeading);
  }

  // "#RRGGBB", or "#RRGGBBAA" when the color is not opaque
  std::string FormatEditorThemeColor(const glm::vec4& color);
  bool ParseEditorThemeColor(std::string_view text, glm::vec4& outColor);

  // Pulls hand-edited values back into a range ImGui can lay out and draw with.
  void ClampEditorTheme(EditorTheme& theme);
}
