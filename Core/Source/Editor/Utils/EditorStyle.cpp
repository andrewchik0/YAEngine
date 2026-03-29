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

    colors[ImGuiCol_WindowBg]             = ImVec4(0.09f, 0.09f, 0.08f, 1.00f);
    colors[ImGuiCol_ChildBg]              = ImVec4(0.09f, 0.09f, 0.08f, 1.00f);
    colors[ImGuiCol_PopupBg]              = ImVec4(0.07f, 0.07f, 0.06f, 0.95f);
    colors[ImGuiCol_Border]               = ImVec4(0.22f, 0.22f, 0.21f, 0.50f);

    colors[ImGuiCol_FrameBg]              = ImVec4(0.13f, 0.13f, 0.12f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.16f, 0.16f, 0.15f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.20f, 0.20f, 0.19f, 1.00f);

    colors[ImGuiCol_TitleBg]              = ImVec4(0.06f, 0.06f, 0.05f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.07f, 0.07f, 0.06f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.06f, 0.06f, 0.05f, 0.75f);

    colors[ImGuiCol_MenuBarBg]            = ImVec4(0.07f, 0.07f, 0.06f, 1.00f);

    colors[ImGuiCol_Tab]                  = ImVec4(0.09f, 0.09f, 0.08f, 1.00f);
    colors[ImGuiCol_TabHovered]           = ImVec4(0.28f, 0.28f, 0.27f, 0.80f);
    colors[ImGuiCol_TabSelected]          = ImVec4(0.20f, 0.20f, 0.19f, 1.00f);
    colors[ImGuiCol_TabDimmed]            = ImVec4(0.06f, 0.06f, 0.05f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected]    = ImVec4(0.16f, 0.16f, 0.15f, 1.00f);

    colors[ImGuiCol_Header]              = ImVec4(0.15f, 0.15f, 0.14f, 1.00f);
    colors[ImGuiCol_HeaderHovered]       = ImVec4(0.24f, 0.24f, 0.23f, 0.80f);
    colors[ImGuiCol_HeaderActive]        = ImVec4(0.20f, 0.20f, 0.19f, 1.00f);

    colors[ImGuiCol_Button]              = ImVec4(0.15f, 0.15f, 0.14f, 1.00f);
    colors[ImGuiCol_ButtonHovered]       = ImVec4(0.24f, 0.24f, 0.23f, 0.80f);
    colors[ImGuiCol_ButtonActive]        = ImVec4(0.20f, 0.20f, 0.19f, 1.00f);

    colors[ImGuiCol_SliderGrab]          = ImVec4(0.33f, 0.33f, 0.32f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]    = ImVec4(0.40f, 0.40f, 0.39f, 1.00f);

    colors[ImGuiCol_CheckMark]           = ImVec4(0.62f, 0.62f, 0.60f, 1.00f);

    colors[ImGuiCol_Separator]           = ImVec4(0.22f, 0.22f, 0.21f, 0.50f);
    colors[ImGuiCol_SeparatorHovered]    = ImVec4(0.30f, 0.30f, 0.29f, 0.80f);
    colors[ImGuiCol_SeparatorActive]     = ImVec4(0.38f, 0.38f, 0.37f, 1.00f);

    colors[ImGuiCol_ResizeGrip]          = ImVec4(0.30f, 0.30f, 0.29f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered]   = ImVec4(0.30f, 0.30f, 0.29f, 0.67f);
    colors[ImGuiCol_ResizeGripActive]    = ImVec4(0.38f, 0.38f, 0.37f, 0.95f);

    colors[ImGuiCol_DockingPreview]      = ImVec4(0.33f, 0.33f, 0.32f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg]      = ImVec4(0.06f, 0.06f, 0.05f, 1.00f);

    colors[ImGuiCol_TextSelectedBg]      = ImVec4(0.25f, 0.25f, 0.24f, 0.35f);

    colors[ImGuiCol_ScrollbarBg]         = ImVec4(0.07f, 0.07f, 0.06f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab]       = ImVec4(0.21f, 0.21f, 0.20f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.24f, 0.24f, 0.23f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.28f, 0.28f, 0.27f, 1.00f);

    colors[ImGuiCol_PlotLinesHovered]    = ImVec4(0.40f, 0.40f, 0.39f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]= ImVec4(0.40f, 0.40f, 0.39f, 1.00f);

    colors[ImGuiCol_TreeLines]            = ImVec4(0.55f, 0.55f, 0.53f, 0.60f);

    colors[ImGuiCol_DragDropTarget]      = ImVec4(0.45f, 0.45f, 0.43f, 0.90f);
    colors[ImGuiCol_DragDropTargetBg]    = ImVec4(0.22f, 0.22f, 0.21f, 0.90f);
    colors[ImGuiCol_NavCursor]           = ImVec4(0.40f, 0.40f, 0.39f, 0.80f);
    colors[ImGuiCol_NavWindowingHighlight]= ImVec4(0.55f, 0.55f, 0.53f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]   = ImVec4(0.15f, 0.15f, 0.14f, 0.73f);

    colors[ImGuiCol_Text]                = ImVec4(0.91f, 0.91f, 0.89f, 1.00f);
    colors[ImGuiCol_TextDisabled]        = ImVec4(0.48f, 0.48f, 0.46f, 1.00f);

    style.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesToNodes;
    style.TreeLinesSize  = 1.0f;
  }
}
