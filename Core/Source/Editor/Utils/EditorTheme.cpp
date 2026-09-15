#include "Editor/Utils/EditorTheme.h"

namespace YAEngine
{
  namespace
  {
    constexpr float MAX_METRIC = 64.0f;
    constexpr float MIN_FONT_SIZE = 8.0f;
    constexpr float MAX_FONT_SIZE = 48.0f;

    void ClampToken(glm::vec4& color)
    {
      color = glm::clamp(color, glm::vec4(0.0f), glm::vec4(1.0f));
    }

    void ClampToken(glm::vec2& value)
    {
      value = glm::clamp(value, glm::vec2(0.0f), glm::vec2(MAX_METRIC));
    }

    void ClampToken(float& value)
    {
      value = std::clamp(value, 0.0f, MAX_METRIC);
    }

    uint32_t ToByte(float channel)
    {
      return uint32_t(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255.0f));
    }

    int32_t HexDigit(char c)
    {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    }

    // One variant of a palette as sRGB hex; the overlay token is derived from the panel color
    struct PaletteColors
    {
      uint32_t appBackground = 0;
      uint32_t panel = 0;
      uint32_t frame = 0;
      uint32_t frameHovered = 0;
      uint32_t frameActive = 0;
      uint32_t popup = 0;
      uint32_t borderSubtle = 0;
      uint32_t borderStrong = 0;
      uint32_t textPrimary = 0;
      uint32_t textSecondary = 0;
      uint32_t textDisabled = 0;
      uint32_t accent = 0;
      uint32_t accentHovered = 0;
      uint32_t accentActive = 0;
      uint32_t accentMuted = 0;
    };

    struct PaletteEntry
    {
      const char* name = nullptr;
      const char* key = nullptr;
      PaletteColors dark;
      PaletteColors light;
    };

    // Surfaces carry a faint tint of the accent's hue. accentMuted also fills sliders, so it must stand
    // apart from the frame color, not only from the panel.
    constexpr PaletteEntry PALETTES[] = {
      {
        .name = "Lavender", .key = "lavender",
        .dark = {
          .appBackground = 0x1F1E23, .panel = 0x2C2B31, .frame = 0x3A383F, .frameHovered = 0x45434B, .frameActive = 0x504E57,
          .popup = 0x2C2B31, .borderSubtle = 0x45434B, .borderStrong = 0x5C5A63,
          .textPrimary = 0xE0DEE6, .textSecondary = 0xA09DAA, .textDisabled = 0x716E7B,
          .accent = 0xA99BDB, .accentHovered = 0xBBAFE6, .accentActive = 0x9586C9, .accentMuted = 0x453D66,
        },
        .light = {
          .appBackground = 0xE6E4EC, .panel = 0xF5F4F8, .frame = 0xE7E5EE, .frameHovered = 0xDCD9E5, .frameActive = 0xD1CDDC,
          .popup = 0xFFFFFF, .borderSubtle = 0xD8D5E0, .borderStrong = 0xBCB7C9,
          .textPrimary = 0x211F28, .textSecondary = 0x5E5A6B, .textDisabled = 0x9A96A6,
          .accent = 0x7A68C4, .accentHovered = 0x6A57B5, .accentActive = 0x5C4AA3, .accentMuted = 0xD8CFF3,
        },
      },
      {
        .name = "Sage", .key = "sage",
        .dark = {
          .appBackground = 0x1D1F1E, .panel = 0x2A2C2B, .frame = 0x373A38, .frameHovered = 0x424542, .frameActive = 0x4D514E,
          .popup = 0x2A2C2B, .borderSubtle = 0x424542, .borderStrong = 0x585C59,
          .textPrimary = 0xDDE0DC, .textSecondary = 0x9CA39D, .textDisabled = 0x6E746F,
          .accent = 0x8DB596, .accentHovered = 0xA3C6AA, .accentActive = 0x789F81, .accentMuted = 0x36503F,
        },
        .light = {
          .appBackground = 0xE3E7E4, .panel = 0xF3F6F4, .frame = 0xE4E9E5, .frameHovered = 0xD8DED9, .frameActive = 0xCCD4CE,
          .popup = 0xFFFFFF, .borderSubtle = 0xD2D9D4, .borderStrong = 0xB4BEB7,
          .textPrimary = 0x1E2420, .textSecondary = 0x58625B, .textDisabled = 0x939D96,
          .accent = 0x4F8A5E, .accentHovered = 0x427A51, .accentActive = 0x376A45, .accentMuted = 0xCBE3D1,
        },
      },
      {
        .name = "Sand", .key = "sand",
        .dark = {
          .appBackground = 0x211F1D, .panel = 0x2E2B29, .frame = 0x3B3835, .frameHovered = 0x47433F, .frameActive = 0x524E49,
          .popup = 0x2E2B29, .borderSubtle = 0x47433F, .borderStrong = 0x5E5954,
          .textPrimary = 0xE3DED7, .textSecondary = 0xA59E95, .textDisabled = 0x766F67,
          .accent = 0xD4A373, .accentHovered = 0xE0B68C, .accentActive = 0xBE8D5E, .accentMuted = 0x55422C,
        },
        .light = {
          .appBackground = 0xEAE5DF, .panel = 0xF8F5F1, .frame = 0xEBE6DF, .frameHovered = 0xE0DAD2, .frameActive = 0xD5CEC4,
          .popup = 0xFFFEFC, .borderSubtle = 0xDCD5CC, .borderStrong = 0xC2B9AD,
          .textPrimary = 0x2A241E, .textSecondary = 0x665D53, .textDisabled = 0xA0968B,
          .accent = 0xB07635, .accentHovered = 0x9C662B, .accentActive = 0x8A5823, .accentMuted = 0xEED6B8,
        },
      },
      {
        .name = "Teal", .key = "teal",
        .dark = {
          .appBackground = 0x1E2124, .panel = 0x2A2E32, .frame = 0x363B40, .frameHovered = 0x42474D, .frameActive = 0x4D5359,
          .popup = 0x2A2E32, .borderSubtle = 0x42474D, .borderStrong = 0x586067,
          .textPrimary = 0xDCE1E4, .textSecondary = 0x9AA3A9, .textDisabled = 0x6C757B,
          .accent = 0x7FB5B5, .accentHovered = 0x96C5C5, .accentActive = 0x6AA0A0, .accentMuted = 0x2F5252,
        },
        .light = {
          .appBackground = 0xE2E8EA, .panel = 0xF2F6F7, .frame = 0xE3EAEC, .frameHovered = 0xD7DFE2, .frameActive = 0xCAD4D8,
          .popup = 0xFFFFFF, .borderSubtle = 0xD1DADD, .borderStrong = 0xB2BFC4,
          .textPrimary = 0x1C2427, .textSecondary = 0x56636A, .textDisabled = 0x919EA4,
          .accent = 0x3E8A8A, .accentHovered = 0x337979, .accentActive = 0x2A6969, .accentMuted = 0xC6E3E3,
        },
      },
      {
        .name = "Rose", .key = "rose",
        .dark = {
          .appBackground = 0x211E1E, .panel = 0x2E2A2A, .frame = 0x3B3636, .frameHovered = 0x474141, .frameActive = 0x524B4B,
          .popup = 0x2E2A2A, .borderSubtle = 0x474141, .borderStrong = 0x5E5757,
          .textPrimary = 0xE4DCDA, .textSecondary = 0xA69C99, .textDisabled = 0x776D6B,
          .accent = 0xCF8E83, .accentHovered = 0xDCA398, .accentActive = 0xBA786D, .accentMuted = 0x573935,
        },
        .light = {
          .appBackground = 0xEBE4E3, .panel = 0xF8F4F3, .frame = 0xEDE5E4, .frameHovered = 0xE2D9D8, .frameActive = 0xD7CCCA,
          .popup = 0xFFFDFD, .borderSubtle = 0xDDD3D1, .borderStrong = 0xC4B6B3,
          .textPrimary = 0x2A2120, .textSecondary = 0x675A58, .textDisabled = 0xA29492,
          .accent = 0xB5655A, .accentHovered = 0xA2574D, .accentActive = 0x8F4A41, .accentMuted = 0xEFCFC9,
        },
      },
      {
        .name = "Blue", .key = "blue",
        .dark = {
          .appBackground = 0x1E1F22, .panel = 0x2B2D30, .frame = 0x393B40, .frameHovered = 0x43454A, .frameActive = 0x4E5157,
          .popup = 0x2B2D30, .borderSubtle = 0x43454A, .borderStrong = 0x5A5D63,
          .textPrimary = 0xDFE1E5, .textSecondary = 0x9DA0A8, .textDisabled = 0x6F737A,
          .accent = 0x4C8DFF, .accentHovered = 0x6BA0FF, .accentActive = 0x3A78E8, .accentMuted = 0x2E436E,
        },
        .light = {
          .appBackground = 0xE3E6EB, .panel = 0xF3F5F8, .frame = 0xE4E7EC, .frameHovered = 0xD9DDE3, .frameActive = 0xCDD2DA,
          .popup = 0xFFFFFF, .borderSubtle = 0xD3D8DF, .borderStrong = 0xB6BDC7,
          .textPrimary = 0x1D2127, .textSecondary = 0x58606B, .textDisabled = 0x959CA6,
          .accent = 0x3B78E0, .accentHovered = 0x2F68CB, .accentActive = 0x2759B0, .accentMuted = 0xC9DBF7,
        },
      },
      {
        .name = "Monochrome", .key = "monochrome",
        .dark = {
          .appBackground = 0x1C1C1C, .panel = 0x282828, .frame = 0x353535, .frameHovered = 0x404040, .frameActive = 0x4B4B4B,
          .popup = 0x282828, .borderSubtle = 0x404040, .borderStrong = 0x575757,
          .textPrimary = 0xDEDEDE, .textSecondary = 0x9C9C9C, .textDisabled = 0x6E6E6E,
          .accent = 0xC4C4C4, .accentHovered = 0xD6D6D6, .accentActive = 0xADADAD, .accentMuted = 0x474747,
        },
        .light = {
          .appBackground = 0xE4E4E4, .panel = 0xF4F4F4, .frame = 0xE6E6E6, .frameHovered = 0xDBDBDB, .frameActive = 0xCFCFCF,
          .popup = 0xFFFFFF, .borderSubtle = 0xD6D6D6, .borderStrong = 0xBABABA,
          .textPrimary = 0x1E1E1E, .textSecondary = 0x5C5C5C, .textDisabled = 0x999999,
          .accent = 0x4A4A4A, .accentHovered = 0x333333, .accentActive = 0x222222, .accentMuted = 0xD0D0D0,
        },
      },
    };
    static_assert(std::size(PALETTES) == size_t(EditorThemePalette::Count));

    // Status and axis colors are shared by every palette of a mode; the light ones are darker to keep
    // their contrast on light surfaces
    struct ModeEntry
    {
      const char* name = nullptr;
      const char* key = nullptr;
      // Of the overlay token, the panel color drawn over the viewport image
      uint8_t overlayAlpha = 0xFF;
      uint32_t success = 0;
      uint32_t warning = 0;
      uint32_t error = 0;
      uint32_t info = 0;
      uint32_t axisX = 0;
      uint32_t axisY = 0;
      uint32_t axisZ = 0;
    };

    constexpr ModeEntry MODES[] = {
      {
        .name = "Dark", .key = "dark", .overlayAlpha = 0xBF,
        .success = 0x5FB865, .warning = 0xE5A84B, .error = 0xE5534B, .info = 0x6BA0FF,
        .axisX = 0xE5534B, .axisY = 0x6CC24A, .axisZ = 0x4C8DFF,
      },
      {
        .name = "Light", .key = "light", .overlayAlpha = 0xD9,
        .success = 0x3D8B45, .warning = 0xB06F16, .error = 0xC9433B, .info = 0x3A72D0,
        .axisX = 0xCC4339, .axisY = 0x45932A, .axisZ = 0x3A72D0,
      },
    };
    static_assert(std::size(MODES) == size_t(EditorThemeMode::Count));
  }

  std::string FormatEditorThemeColor(const glm::vec4& color)
  {
    char text[16];
    uint32_t alpha = ToByte(color.a);
    if (alpha == 0xFF)
      snprintf(text, sizeof(text), "#%02X%02X%02X", ToByte(color.r), ToByte(color.g), ToByte(color.b));
    else
      snprintf(text, sizeof(text), "#%02X%02X%02X%02X", ToByte(color.r), ToByte(color.g), ToByte(color.b), alpha);
    return text;
  }

  bool ParseEditorThemeColor(std::string_view text, glm::vec4& outColor)
  {
    if (!text.empty() && text.front() == '#')
      text.remove_prefix(1);
    if (text.size() != 6 && text.size() != 8)
      return false;

    uint32_t value = 0;
    for (char c : text)
    {
      int32_t digit = HexDigit(c);
      if (digit < 0)
        return false;
      value = (value << 4) | uint32_t(digit);
    }

    if (text.size() == 6)
      outColor = EditorThemeColor(value);
    else
      outColor = EditorThemeColor(value >> 8, uint8_t(value & 0xFF));
    return true;
  }

  void ClampEditorTheme(EditorTheme& theme)
  {
    VisitEditorThemeTokens([&](const char*, auto member) { ClampToken(theme.*member); });

    theme.labelColumnFraction = std::clamp(theme.labelColumnFraction, 0.1f, 0.9f);
    theme.fontSizeBody = std::clamp(theme.fontSizeBody, MIN_FONT_SIZE, MAX_FONT_SIZE);
    theme.fontSizeSmall = std::clamp(theme.fontSizeSmall, MIN_FONT_SIZE, MAX_FONT_SIZE);
    theme.fontSizeHeading = std::clamp(theme.fontSizeHeading, MIN_FONT_SIZE, MAX_FONT_SIZE);
  }

  const char* GetEditorThemePaletteName(EditorThemePalette palette)
  {
    return PALETTES[size_t(palette)].name;
  }

  const char* GetEditorThemePaletteKey(EditorThemePalette palette)
  {
    return PALETTES[size_t(palette)].key;
  }

  const char* GetEditorThemeModeName(EditorThemeMode mode)
  {
    return MODES[size_t(mode)].name;
  }

  const char* GetEditorThemeModeKey(EditorThemeMode mode)
  {
    return MODES[size_t(mode)].key;
  }

  bool ParseEditorThemePalette(std::string_view key, EditorThemePalette& outPalette)
  {
    for (size_t i = 0; i < std::size(PALETTES); i++)
    {
      if (key == PALETTES[i].key)
      {
        outPalette = EditorThemePalette(i);
        return true;
      }
    }
    return false;
  }

  bool ParseEditorThemeMode(std::string_view key, EditorThemeMode& outMode)
  {
    for (size_t i = 0; i < std::size(MODES); i++)
    {
      if (key == MODES[i].key)
      {
        outMode = EditorThemeMode(i);
        return true;
      }
    }
    return false;
  }

  void ApplyEditorThemePreset(EditorTheme& theme, EditorThemePreset preset)
  {
    const PaletteEntry& palette = PALETTES[size_t(preset.palette)];
    const PaletteColors& colors = preset.mode == EditorThemeMode::Light ? palette.light : palette.dark;
    const ModeEntry& mode = MODES[size_t(preset.mode)];

    theme.appBackground = EditorThemeColor(colors.appBackground);
    theme.panel = EditorThemeColor(colors.panel);
    theme.frame = EditorThemeColor(colors.frame);
    theme.frameHovered = EditorThemeColor(colors.frameHovered);
    theme.frameActive = EditorThemeColor(colors.frameActive);
    theme.overlay = EditorThemeColor(colors.panel, mode.overlayAlpha);
    theme.popup = EditorThemeColor(colors.popup);

    theme.borderSubtle = EditorThemeColor(colors.borderSubtle);
    theme.borderStrong = EditorThemeColor(colors.borderStrong);

    theme.textPrimary = EditorThemeColor(colors.textPrimary);
    theme.textSecondary = EditorThemeColor(colors.textSecondary);
    theme.textDisabled = EditorThemeColor(colors.textDisabled);

    theme.accent = EditorThemeColor(colors.accent);
    theme.accentHovered = EditorThemeColor(colors.accentHovered);
    theme.accentActive = EditorThemeColor(colors.accentActive);
    theme.accentMuted = EditorThemeColor(colors.accentMuted);

    theme.success = EditorThemeColor(mode.success);
    theme.warning = EditorThemeColor(mode.warning);
    theme.error = EditorThemeColor(mode.error);
    theme.info = EditorThemeColor(mode.info);

    theme.axisX = EditorThemeColor(mode.axisX);
    theme.axisY = EditorThemeColor(mode.axisY);
    theme.axisZ = EditorThemeColor(mode.axisZ);
  }

  EditorTheme MakeEditorTheme(EditorThemePreset preset)
  {
    EditorTheme theme;
    ApplyEditorThemePreset(theme, preset);
    return theme;
  }
}
