#pragma once

#include "Editor/IEditorPanel.h"
#include "Editor/EditorContext.h"
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
    uint32_t m_LastWidth = 0;
    uint32_t m_LastHeight = 0;
  };
}
