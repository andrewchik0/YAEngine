#include "EditorStyle.h"

#include <imgui.h>

namespace YAEngine
{
  void EditorStyle::Apply()
  {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding    = 4.0f;
    style.FrameRounding     = 2.0f;
    style.GrabRounding      = 2.0f;
    style.TabRounding       = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.ChildRounding     = 2.0f;
    style.PopupRounding     = 3.0f;

    style.WindowPadding  = ImVec2(8.0f, 8.0f);
    style.FramePadding   = ImVec2(6.0f, 3.0f);
    style.ItemSpacing    = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    style.IndentSpacing  = 16.0f;
    style.ScrollbarSize  = 14.0f;
    style.GrabMinSize    = 10.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize  = 0.0f;
    style.TabBorderSize    = 0.0f;

    ImVec4* colors = style.Colors;

    colors[ImGuiCol_WindowBg]             = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_ChildBg]              = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_PopupBg]              = ImVec4(0.12f, 0.12f, 0.12f, 0.95f);
    colors[ImGuiCol_Border]               = ImVec4(0.30f, 0.30f, 0.30f, 0.50f);

    colors[ImGuiCol_FrameBg]              = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);

    colors[ImGuiCol_TitleBg]              = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.10f, 0.10f, 0.10f, 0.75f);

    colors[ImGuiCol_MenuBarBg]            = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);

    colors[ImGuiCol_Tab]                  = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_TabHovered]           = ImVec4(0.28f, 0.45f, 0.70f, 0.80f);
    colors[ImGuiCol_TabSelected]          = ImVec4(0.22f, 0.38f, 0.60f, 1.00f);
    colors[ImGuiCol_TabDimmed]            = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected]    = ImVec4(0.18f, 0.30f, 0.50f, 1.00f);

    colors[ImGuiCol_Header]              = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_HeaderHovered]       = ImVec4(0.28f, 0.45f, 0.70f, 0.80f);
    colors[ImGuiCol_HeaderActive]        = ImVec4(0.22f, 0.38f, 0.60f, 1.00f);

    colors[ImGuiCol_Button]              = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_ButtonHovered]       = ImVec4(0.28f, 0.45f, 0.70f, 0.80f);
    colors[ImGuiCol_ButtonActive]        = ImVec4(0.22f, 0.38f, 0.60f, 1.00f);

    colors[ImGuiCol_SliderGrab]          = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]    = ImVec4(0.35f, 0.55f, 0.80f, 1.00f);

    colors[ImGuiCol_CheckMark]           = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);

    colors[ImGuiCol_Separator]           = ImVec4(0.30f, 0.30f, 0.30f, 0.50f);
    colors[ImGuiCol_SeparatorHovered]    = ImVec4(0.28f, 0.45f, 0.70f, 0.80f);
    colors[ImGuiCol_SeparatorActive]     = ImVec4(0.22f, 0.38f, 0.60f, 1.00f);

    colors[ImGuiCol_ResizeGrip]          = ImVec4(0.28f, 0.45f, 0.70f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered]   = ImVec4(0.28f, 0.45f, 0.70f, 0.67f);
    colors[ImGuiCol_ResizeGripActive]    = ImVec4(0.22f, 0.38f, 0.60f, 0.95f);

    colors[ImGuiCol_DockingPreview]      = ImVec4(0.28f, 0.45f, 0.70f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg]      = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);

    colors[ImGuiCol_TextSelectedBg]      = ImVec4(0.28f, 0.45f, 0.70f, 0.35f);

    colors[ImGuiCol_ScrollbarBg]         = ImVec4(0.12f, 0.12f, 0.12f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab]       = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);

    colors[ImGuiCol_Text]                = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_TextDisabled]        = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
  }
}
