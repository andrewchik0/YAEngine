#pragma once

#include "Pch.h"
#include "Editor/Utils/EditorTheme.h"

#include <imgui.h>

namespace YAEngine
{
  // Theme tokens are display (sRGB) colors, but ImGui draws into an sRGB swapchain that encodes
  // whatever it is given, so they are decoded to linear first or every surface washes out.
  inline ImVec4 ToImGuiColor(const glm::vec4& color)
  {
    auto decode = [](float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
    return ImVec4(decode(color.r), decode(color.g), decode(color.b), color.a);
  }

  class EditorStyle
  {
  public:
    // Maps the theme onto ImGuiStyle and, when its context exists, the ImPlot style, with sizes
    // multiplied by the window content scale. Call again whenever tokens or the scale change.
    static void Apply(const EditorTheme& theme, float contentScale);
    // ImGui's style must not change inside a frame, so a theme changed during one is requested and applied
    // by ApplyRequestedTheme between frames; a later request replaces an earlier one
    static void RequestTheme(const EditorTheme& theme);
    static void ApplyRequestedTheme();
    // The requested theme, or the applied one when no request is waiting
    static const EditorTheme& GetRequestedTheme();
    // For an ImPlot context created after the last Apply
    static void ApplyPlotStyle();

    static const EditorTheme& GetTheme();
    static float GetContentScale();
  };
}
