#pragma once

#include "Pch.h"

struct ImFont;

namespace YAEngine
{
  struct EditorTheme;

  enum class EditorFontRole : uint8_t
  {
    Body,   // Inter Regular
    Medium, // Inter Medium: group headers, tabs
    Strong, // Inter SemiBold: panel titles, emphasis
    Mono,   // JetBrains Mono Regular: numeric readouts, paths, tables
    Count
  };

  // Every role carries the Lucide icons, so ICON_LC_* strings render in any of them.
  class EditorFonts
  {
  public:
    // Once, before the first editor frame. Body becomes the default font.
    static void Load(const EditorTheme& theme);

    // Null when the role's font file could not be loaded
    static ImFont* Get(EditorFontRole role);
    // Unscaled size Push(role) uses: the heading size for Strong, the body size otherwise
    static float GetDefaultSize(EditorFontRole role);

    static void Push(EditorFontRole role);
    static void Push(EditorFontRole role, float size);
    static void Pop();
  };
}
