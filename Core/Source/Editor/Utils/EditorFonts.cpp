#include "Editor/Utils/EditorFonts.h"

#include <imgui.h>
#include <misc/freetype/imgui_freetype.h>

#include "Editor/Utils/EditorIcons.h"
#include "Editor/Utils/EditorStyle.h"
#include "Utils/Log.h"

namespace YAEngine
{
  namespace
  {
    constexpr const char* ROLE_FILES[] = {
      "Inter-Regular.ttf",
      "Inter-Medium.ttf",
      "Inter-SemiBold.ttf",
      "JetBrainsMono-Regular.ttf",
    };

    // Lucide glyphs fill the whole em above the baseline, while text fonts spend part of their
    // size below it. These shrink icons to sit a little above cap height and move them down onto
    // the text's optical center. Fractions of the font size, so every pushed size agrees.
    constexpr float ICON_SIZE_RATIO = 0.9375f;
    constexpr float ICON_OFFSET_Y_RATIO = 0.1875f;

    // Inter and JetBrains Mono carry private use glyphs inside the Lucide range, and the first
    // source that has a glyph wins, so the text sources must not claim those codepoints.
    constexpr ImWchar ICON_RANGE[] = { ICON_MIN_LC, ICON_MAX_LC, 0 };

    ImFont* s_Fonts[size_t(EditorFontRole::Count)] = {};
  }

  void EditorFonts::Load(const EditorTheme& theme)
  {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->FontLoaderFlags |= ImGuiFreeTypeLoaderFlags_LightHinting;

    const std::string directory = WORKING_DIR "/Assets/Fonts/";
    const std::string iconPath = directory + FONT_ICON_FILE_NAME_LC;
    const bool hasIcons = std::filesystem::exists(iconPath);
    if (!hasIcons)
      YA_LOG_ERROR("Editor", "Fonts: icon font '%s' is missing, icons will not render", iconPath.c_str());

    const float size = theme.fontSizeBody;
    for (size_t role = 0; role < size_t(EditorFontRole::Count); role++)
    {
      const std::string path = directory + ROLE_FILES[role];
      if (!std::filesystem::exists(path))
      {
        YA_LOG_ERROR("Editor", "Fonts: '%s' is missing, the role falls back to the default font", path.c_str());
        continue;
      }

      ImFontConfig textConfig;
      textConfig.GlyphExcludeRanges = ICON_RANGE;
      ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), size, &textConfig);

      if (font != nullptr && hasIcons)
      {
        ImFontConfig iconConfig;
        iconConfig.MergeMode = true;
        iconConfig.GlyphOffset = ImVec2(0.0f, size * ICON_OFFSET_Y_RATIO);
        io.Fonts->AddFontFromFileTTF(iconPath.c_str(), size * ICON_SIZE_RATIO, &iconConfig);
      }

      s_Fonts[role] = font;
    }

    if (ImFont* body = s_Fonts[size_t(EditorFontRole::Body)])
      io.FontDefault = body;
  }

  ImFont* EditorFonts::Get(EditorFontRole role)
  {
    return s_Fonts[size_t(role)];
  }

  float EditorFonts::GetDefaultSize(EditorFontRole role)
  {
    const EditorTheme& theme = EditorStyle::GetTheme();
    return role == EditorFontRole::Strong ? theme.fontSizeHeading : theme.fontSizeBody;
  }

  void EditorFonts::Push(EditorFontRole role)
  {
    Push(role, GetDefaultSize(role));
  }

  void EditorFonts::Push(EditorFontRole role, float size)
  {
    ImGui::PushFont(Get(role), size);
  }

  void EditorFonts::Pop()
  {
    ImGui::PopFont();
  }
}
