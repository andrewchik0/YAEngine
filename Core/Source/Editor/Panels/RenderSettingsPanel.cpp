#include "Editor/Panels/RenderSettingsPanel.h"

#include <imgui.h>

#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorIcons.h"
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

    if (ImGui::CollapsingHeader(ICON_FA_DISPLAY " Display", ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::DragFloat("Gamma", &context.render->GetGamma(), 0.01f, 0.0f, 10.0f);
      ImGui::DragFloat("Exposure", &context.render->GetExposure(), 0.01f, 0.0f, 10.0f);

      const char* tonemappers[] = { "ACES", "AgX" }; // indices match TONEMAP_ACES, TONEMAP_AGX
      ImGui::Combo("Tonemapper", &context.render->GetTonemapMode(), tonemappers, IM_ARRAYSIZE(tonemappers));

      int debugViewIndex = context.render->GetDebugView();
      const char* debugViews[] = {
        "Off", "Albedo", "Metallic", "Roughness", "Normals", "SSAO", "SSR"
      };
      if (ImGui::Combo("Debug View", &debugViewIndex, debugViews, IM_ARRAYSIZE(debugViews)))
        context.render->SetDebugView(debugViewIndex);
    }

    if (ImGui::CollapsingHeader(ICON_FA_SLIDERS " Post-Processing", ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::Checkbox("SSAO", &context.render->GetSSAOEnabled());
      ImGui::Checkbox("SSR", &context.render->GetSSREnabled());
      ImGui::Checkbox("TAA", &context.render->GetTAAEnabled());

      ImGui::Checkbox("Bloom", &context.render->GetBloomEnabled());
      if (context.render->GetBloomEnabled())
      {
        ImGui::DragFloat("Bloom Intensity", &context.render->GetBloomIntensity(), 0.001f, 0.0f, 1.0f);
        ImGui::DragFloat("Bloom Threshold", &context.render->GetBloomThreshold(), 0.01f, 0.0f, 5.0f);
        ImGui::DragFloat("Bloom Soft Knee", &context.render->GetBloomSoftKnee(), 0.01f, 0.0f, 1.0f);
      }

      ImGui::Separator();
      ImGui::Checkbox("Auto Exposure", &context.render->GetAutoExposureEnabled());
      if (context.render->GetAutoExposureEnabled())
      {
        ImGui::DragFloat("Speed Up", &context.render->GetAdaptSpeedUp(), 0.1f, 0.1f, 10.0f);
        ImGui::DragFloat("Speed Down", &context.render->GetAdaptSpeedDown(), 0.1f, 0.1f, 10.0f);
        ImGui::DragFloat("Low Percentile", &context.render->GetLowPercentile(), 0.01f, 0.01f, 0.5f);
        ImGui::DragFloat("High Percentile", &context.render->GetHighPercentile(), 0.01f, 0.5f, 0.99f);
      }
    }

    if (ImGui::CollapsingHeader(ICON_FA_WRENCH " Debug", ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::Checkbox("Gizmos", &context.render->GetGizmosEnabled());

      if (ImGui::Button(ICON_FA_ROTATE " Recompile Shaders"))
        context.render->GetShaderHotReload().RecompileAll();
    }

    ImGui::End();
  }
}
