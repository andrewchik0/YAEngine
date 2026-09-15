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
}
