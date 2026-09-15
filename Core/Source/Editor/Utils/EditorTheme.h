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

  // Built-in color sets; each has a dark and a light variant
  enum class EditorThemePalette : uint8_t
  {
    Lavender,
    Sage,
    Sand,
    Teal,
    Rose,
    Blue,
    Monochrome,
    Count
  };

  enum class EditorThemeMode : uint8_t
  {
    Dark,
    Light,
    Count
  };

  struct EditorThemePreset
  {
    EditorThemePalette palette = EditorThemePalette::Lavender;
    EditorThemeMode mode = EditorThemeMode::Dark;

    bool operator==(const EditorThemePreset&) const = default;
  };

  // The look of the editor as plain data, independent of ImGui. EditorStyle maps it onto
  // ImGuiStyle and ImPlot and multiplies sizes by the window content scale.
  // Colors come from a preset (MakeEditorTheme); a default-constructed theme has only the metrics.
  struct EditorTheme
  {
    glm::vec4 appBackground {};
    glm::vec4 panel {};
    glm::vec4 frame {};
    glm::vec4 frameHovered {};
    glm::vec4 frameActive {};
    // Strips drawn over the viewport image; the alpha is part of the token
    glm::vec4 overlay {};
    glm::vec4 popup {};

    glm::vec4 borderSubtle {};
    glm::vec4 borderStrong {};

    glm::vec4 textPrimary {};
    glm::vec4 textSecondary {};
    glm::vec4 textDisabled {};

    glm::vec4 accent {};
    glm::vec4 accentHovered {};
    glm::vec4 accentActive {};
    // Selection backgrounds and slider fills
    glm::vec4 accentMuted {};

    glm::vec4 success {};
    glm::vec4 warning {};
    glm::vec4 error {};
    glm::vec4 info {};

    glm::vec4 axisX {};
    glm::vec4 axisY {};
    glm::vec4 axisZ {};

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

  // Display name ("Lavender") and editor.yaml key ("lavender")
  const char* GetEditorThemePaletteName(EditorThemePalette palette);
  const char* GetEditorThemePaletteKey(EditorThemePalette palette);
  const char* GetEditorThemeModeName(EditorThemeMode mode);
  const char* GetEditorThemeModeKey(EditorThemeMode mode);
  // An unknown key leaves the output untouched
  bool ParseEditorThemePalette(std::string_view key, EditorThemePalette& outPalette);
  bool ParseEditorThemeMode(std::string_view key, EditorThemeMode& outMode);

  // Replaces every color token with the preset's; metrics and font sizes stay
  void ApplyEditorThemePreset(EditorTheme& theme, EditorThemePreset preset);
  // The preset's colors with the default metrics and font sizes
  EditorTheme MakeEditorTheme(EditorThemePreset preset);
}
