#include "Editor/Panels/RenderSettingsPanel.h"

#include <imgui.h>

#include "Editor/EditorContext.h"
#include "Render/Render.h"

namespace YAEngine
{
  void RenderSettingsPanel::OnRender(EditorContext& context)
  {
    if (!ImGui::Begin("Render Settings"))
    {
      ImGui::End();
      return;
    }

    if (!context.render)
    {
      ImGui::TextDisabled("Render not available");
      ImGui::End();
      return;
    }

    // Display section
    if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::DragFloat("Gamma", &context.render->GetGamma(), 0.01f, 0.0f, 10.0f);
      ImGui::DragFloat("Exposure", &context.render->GetExposure(), 0.01f, 0.0f, 10.0f);

      int debugViewIndex = context.render->GetDebugView();
      const char* debugViews[] = {
        "Off", "Albedo", "Metallic", "Roughness", "Normals", "SSAO", "SSR"
      };
      if (ImGui::Combo("Debug View", &debugViewIndex, debugViews, IM_ARRAYSIZE(debugViews)))
        context.render->SetDebugView(debugViewIndex);
    }

    // Post-Processing section
    if (ImGui::CollapsingHeader("Post-Processing", ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::Checkbox("SSAO", &context.render->GetSSAOEnabled());
      ImGui::Checkbox("SSR", &context.render->GetSSREnabled());
      ImGui::Checkbox("TAA", &context.render->GetTAAEnabled());
    }

    ImGui::End();
  }
}
