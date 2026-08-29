#pragma once

#include "Editor/IEditorPanel.h"
#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorIcons.h"
#include "Render/Render.h"

#include <imgui.h>

namespace YAEngine
{
  class ViewportPanel : public IEditorPanel
  {
  public:

    const char* GetName() const override { return "Viewport"; }

    void OnRender(EditorContext& context) override
    {
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
      if (ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
      {
        context.viewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        auto size = ImGui::GetContentRegionAvail();
        uint32_t w = static_cast<uint32_t>(size.x);
        uint32_t h = static_cast<uint32_t>(size.y);

        if (w > 0 && h > 0 && context.render)
        {
          if (w != m_LastWidth || h != m_LastHeight)
          {
            m_LastWidth = w;
            m_LastHeight = h;
            context.viewportWidth = w;
            context.viewportHeight = h;
            context.render->RequestViewportResize(w, h);
          }

          auto vpMin = ImGui::GetCursorScreenPos();
          auto mouse = ImGui::GetMousePos();
          glm::vec2 rel((mouse.x - vpMin.x) / size.x, (mouse.y - vpMin.y) / size.y);
          context.mouseInViewportValid = context.viewportHovered
            && rel.x >= 0.0f && rel.x <= 1.0f && rel.y >= 0.0f && rel.y <= 1.0f;
          context.mouseInViewport = rel;

          ImGui::Image(context.render->GetSceneTextureID(), size);

          DrawPreviewOverlay(context, vpMin);
        }
      }
      else
      {
        context.viewportHovered = false;
      }
      ImGui::End();
      ImGui::PopStyleVar();
    }

  private:
    // The viewport shows the scene camera exactly as the game would, with no editor
    // camera cues left on screen, so the only sign that it is not the editor camera has
    // to be drawn here.
    static void DrawPreviewOverlay(EditorContext& context, const ImVec2& viewportOrigin)
    {
      if (!context.IsPreviewingCamera() || context.scene == nullptr)
        return;
      if (!context.scene->GetRegistry().valid(context.previewCamera))
        return;

      constexpr float PADDING = 10.0f;
      ImGui::SetCursorScreenPos(ImVec2(viewportOrigin.x + PADDING, viewportOrigin.y + PADDING));

      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.06f, 0.75f));
      ImGui::BeginChild("PreviewOverlay", ImVec2(0, 0),
        ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);

      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.25f, 1.0f));
      ImGui::TextUnformatted(ICON_FA_VIDEO " Previewing:");
      ImGui::PopStyleColor();
      ImGui::SameLine();
      ImGui::TextUnformatted(context.scene->GetName(context.previewCamera).c_str());
      ImGui::SameLine();
      if (ImGui::SmallButton("Exit Preview"))
        context.StopCameraPreview();

      // The click that leaves the preview must not also fire a viewport pick
      if (ImGui::IsItemHovered())
        context.mouseInViewportValid = false;

      ImGui::EndChild();
      ImGui::PopStyleColor();
    }

    uint32_t m_LastWidth = 0;
    uint32_t m_LastHeight = 0;
  };
}
