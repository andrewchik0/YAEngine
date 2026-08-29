#include "Editor/Panels/RenderSettingsPanel.h"

#include <imgui.h>

#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorIcons.h"
#include "Editor/Utils/FileDialog.h"
#include "Render/Render.h"
#include "Assets/AssetManager.h"
#include "Scene/Scene.h"

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

    if (ImGui::CollapsingHeader(ICON_FA_CLOUD_SUN " Environment", ImGuiTreeNodeFlags_DefaultOpen))
    {
      auto& scene = *context.scene;
      auto& assets = *context.assetManager;

      CubeMapHandle currentSkybox = scene.GetSkybox();
      std::string skyboxPath = assets.CubeMaps().GetPath(currentSkybox);

      if (!skyboxPath.empty())
      {
        std::string relativePath = assets.MakeRelative(skyboxPath);
        ImGui::Text("Skybox: %s", relativePath.c_str());
      }
      else
      {
        ImGui::TextDisabled("No skybox");
      }

      if (ImGui::Button(ICON_FA_FOLDER_OPEN " Load Skybox..."))
      {
        nfdu8filteritem_t filters[] = {
          { "HDR Images", "hdr" },
        };
        std::string path = FileDialog::OpenFile(filters, 1);
        if (!path.empty())
        {
          auto handle = assets.CubeMaps().Load(path);
          if (handle)
            scene.SetSkybox(handle);
        }
      }

      if (currentSkybox)
      {
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_XMARK " Clear"))
          scene.SetSkybox({});
      }
    }

    if (ImGui::CollapsingHeader(ICON_FA_DISPLAY " Display", ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::DragFloat("Gamma", &context.render->GetGamma(), 0.01f, 0.0f, 10.0f);
      ImGui::DragFloat("Exposure", &context.render->GetExposure(), 0.01f, 0.0f, 10.0f);

      const char* tonemappers[] = { "ACES", "AgX" }; // indices match TONEMAP_ACES, TONEMAP_AGX
      ImGui::Combo("Tonemapper", &context.render->GetTonemapMode(), tonemappers, IM_ARRAYSIZE(tonemappers));

      int debugViewIndex = context.render->GetDebugView();
      // Indices must match the DEBUG_VIEW_* defines in Core/Shared/FrameUniforms.h
      const char* debugViews[] = {
        "Off", "Albedo", "Metallic", "Roughness", "Normals", "AO", "SSR", "Wireframe",
        "TAA Delta", "Velocity",
        "Ambient Only", "Ambient Diffuse", "Ambient Specular",
        "Reflection Probe Index", "Reflection Probe Fallback", "Volume Coverage",
        "SSGI Validity", "SSGI Screen Part", "SSGI Fallback Weight",
        "Direct Only"
      };
      if (ImGui::Combo("Debug View", &debugViewIndex, debugViews, IM_ARRAYSIZE(debugViews)))
        context.render->SetDebugView(debugViewIndex);
    }

    if (ImGui::CollapsingHeader(ICON_FA_SLIDERS " Post-Processing", ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::Checkbox("GTAO", &context.render->GetAOEnabled());
      if (context.render->GetAOEnabled())
      {
        const char* aoQualityLevels[] = { "Low", "Medium", "High", "Ultra" };
        ImGui::Combo("AO Quality", &context.render->GetAOQualityLevel(),
          aoQualityLevels, IM_ARRAYSIZE(aoQualityLevels));
        ImGui::Checkbox("AO Denoise", &context.render->GetAODenoiseEnabled());
        ImGui::DragFloat("AO Radius", &context.render->GetAORadius(), 0.01f, 0.0f, 20.0f);

        // With SSGI on, the diffuse indirect term is gathered for real and neither of
        // these two ever reaches it - only specular occlusion still listens to AO.
        ImGui::BeginDisabled(context.render->GetSSGIEnabled());
        ImGui::DragFloat("AO Strength", &context.render->GetAOStrength(), 0.01f, 0.0f, 1.0f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
          ImGui::SetTooltip("Fades diffuse ambient occlusion. Disabled while SSGI is on:\nSSGI replaces the diffuse occlusion approximation entirely.");
        ImGui::EndDisabled();
        ImGui::DragFloat("AO Specular Strength", &context.render->GetAOSpecularStrength(), 0.01f, 0.0f, 1.0f);
        ImGui::BeginDisabled(context.render->GetSSGIEnabled());
        ImGui::DragFloat("AO Multi Bounce", &context.render->GetAOMultiBounce(), 0.01f, 0.0f, 1.0f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
          ImGui::SetTooltip("Approximated interreflection inside occluded areas. Disabled while\nSSGI is on: SSGI computes that bounce for real.");
        ImGui::EndDisabled();

        // Fitted by Intel against a ray traced ground truth. Worth exposing, not worth
        // touching without a reference image to compare against.
        if (ImGui::TreeNode("AO Heuristics"))
        {
          ImGui::DragFloat("Radius Multiplier", &context.render->GetAORadiusMultiplier(), 0.01f, 0.3f, 3.0f);
          ImGui::DragFloat("Falloff Range", &context.render->GetAOFalloffRange(), 0.01f, 0.0f, 1.0f);
          ImGui::DragFloat("Sample Distribution Power", &context.render->GetAOSampleDistributionPower(), 0.01f, 1.0f, 3.0f);
          ImGui::DragFloat("Thin Occluder Compensation", &context.render->GetAOThinOccluderCompensation(), 0.01f, 0.0f, 0.7f);
          ImGui::DragFloat("Final Value Power", &context.render->GetAOFinalValuePower(), 0.01f, 0.5f, 5.0f);
          ImGui::DragFloat("Depth Mip Sampling Offset", &context.render->GetAODepthMipSamplingOffset(), 0.01f, 0.0f, 30.0f);
          ImGui::TreePop();
        }
      }
      ImGui::Checkbox("SSGI", &context.render->GetSSGIEnabled());
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Screen-space diffuse bounce on the GTAO visibility bitmask.\nRequires GTAO: it rides the same pass and the same samples.");
      if (context.render->GetSSGIEnabled())
      {
        ImGui::DragFloat("SSGI Radius", &context.render->GetSSGIRadius(), 0.05f, 0.1f, 20.0f, "%.2f m");
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("World-space gather radius of the bounce. Wider than the AO radius:\nAO keeps its own falloff at AO Radius from the same samples.");
        ImGui::DragFloat("SSGI Thickness", &context.render->GetSSGIThickness(), 0.01f, 0.01f, 2.0f, "%.2f m");
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Assumed occluder depth behind every screen sample. Thin geometry\n(railings, poles, foliage) stops over-occluding as this goes down.");
        ImGui::DragFloat("SSGI Intensity", &context.render->GetSSGIIntensity(), 0.01f, 0.0f, 4.0f);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Multiplier on the screen-gathered part only. The volume fallback\nis untouched, so 1.0 is the energy-conserving value.");
      }
      ImGui::Checkbox("SSR", &context.render->GetSSREnabled());
      if (context.render->GetSSREnabled())
        ImGui::DragFloat("SSR Intensity", &context.render->GetSSRIntensity(), 0.05f, 0.0f, 20.0f);
      {
        AntialiasingMode& mode = context.render->GetAntialiasingMode();
        bool dlssAvailable = context.render->IsDLSSAvailable();
        const std::string& reason = context.render->GetDLSSUnavailableReason();
        const char* unavailableReason = reason.empty() ? "not supported on this device" : reason.c_str();

        if (ImGui::BeginCombo("Antialiasing", GetAntialiasingModeName(mode)))
        {
          for (uint32_t i = 0; i < uint32_t(AntialiasingMode::Count); i++)
          {
            AntialiasingMode option = AntialiasingMode(i);
            bool selectable = dlssAvailable || !IsDLSSMode(option);

            ImGui::BeginDisabled(!selectable);
            if (ImGui::Selectable(GetAntialiasingModeName(option), option == mode) && selectable)
              mode = option;
            ImGui::EndDisabled();

            if (!selectable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
              ImGui::SetTooltip("DLSS is unavailable: %s", unavailableReason);
          }
          ImGui::EndCombo();
        }

        if (!dlssAvailable)
          ImGui::TextDisabled("DLSS is unavailable: %s", unavailableReason);

        AntialiasingMode effective = context.render->GetEffectiveAntialiasingMode();

        // Only the upscale modes make the two differ; showing it always would just be
        // the viewport size printed twice.
        VkExtent2D renderExtent = context.render->GetRenderExtent();
        VkExtent2D outputExtent = context.render->GetOutputExtent();
        if (renderExtent.width != outputExtent.width || renderExtent.height != outputExtent.height)
          ImGui::Text("Render %ux%u -> output %ux%u",
            renderExtent.width, renderExtent.height, outputExtent.width, outputExtent.height);

        if (UsesTAAPass(effective))
          ImGui::DragFloat("TAA Clamp Sigma", &context.render->GetTAAClampSigma(), 0.01f, 0.0f, 8.0f);
      }

      ImGui::Checkbox("Bloom", &context.render->GetBloomEnabled());
      if (context.render->GetBloomEnabled())
      {
        ImGui::DragFloat("Bloom Intensity", &context.render->GetBloomIntensity(), 0.001f, 0.0f, 1.0f);
        ImGui::DragFloat("Bloom Threshold", &context.render->GetBloomThreshold(), 0.01f, 0.0f, 5.0f);
        ImGui::DragFloat("Bloom Soft Knee", &context.render->GetBloomSoftKnee(), 0.01f, 0.0f, 1.0f);
      }

      ImGui::Separator();
      ImGui::Checkbox("Fog", &context.render->GetFogEnabled());
      if (context.render->GetFogEnabled())
      {
        ImGui::DragFloat("Fog Density", &context.render->GetFogDensity(), 0.001f, 0.0f, 1.0f);
        ImGui::DragFloat("Fog Height Falloff", &context.render->GetFogHeightFalloff(), 0.001f, 0.001f, 1.0f);
        ImGui::ColorEdit3("Fog Color", &context.render->GetFogColor().x);
        ImGui::DragFloat("Fog Max Opacity", &context.render->GetFogMaxOpacity(), 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Fog Start Distance", &context.render->GetFogStartDistance(), 0.5f, 0.0f, 500.0f);
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
      ImGui::Checkbox("Reflection Probe Volumes", &context.render->GetProbeVolumesVisible());
      ImGui::Checkbox("Irradiance Volumes", &context.render->GetIrradianceVolumesVisible());
      ImGui::Checkbox("Volume Nodes", &context.render->GetVolumeNodesVisible());
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Draws the baked SH nodes of the SELECTED volume.\nAbove 20000 nodes only every k-th node is drawn.");
      if (context.render->GetVolumeNodesVisible())
      {
        ImGui::Checkbox("Volume Rejected Nodes", &context.render->GetVolumeInvalidNodesVisible());

        int nodeColorMode = int(context.render->GetVolumeNodeColorMode());
        // Indices must match VolumeNodeColorMode in Render.h
        const char* nodeColorModes[] = { "Irradiance", "Ringing" };
        if (ImGui::Combo("Node Color", &nodeColorMode, nodeColorModes, IM_ARRAYSIZE(nodeColorModes)))
          context.render->GetVolumeNodeColorMode() = VolumeNodeColorMode(nodeColorMode);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Irradiance: L0 divided by the brightest node of the volume, gamma encoded.\n"
            "Black = unlit, white = the brightest node, hue is the color of the bounce.\n\n"
            "Ringing: worst channel of |L1| / L0.\n"
            "Green to yellow = the L1 fit stays positive, yellow means it is close.\n"
            "Opaque red to white = above 1, those nodes reconstruct negative irradiance\n"
            "for some normals and the shader clamps them to black.");
      }
      ImGui::Checkbox("Colliders", &context.render->GetCollidersVisible());
      ImGui::Checkbox("Camera Frustums", &context.render->GetCameraFrustumsVisible());

      ImGui::Separator();
      ImGui::Checkbox("Shadow Cascade LOD", &context.render->GetShadowLodEnabled());
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Distant cascades draw a simplified index stream over the same vertices.\n"
          "Off puts every cascade back on the source mesh.");

      if (context.render->GetShadowLodEnabled())
      {
        int* cascadeLods = context.render->GetShadowCascadeLods();
        for (uint32_t cascade = 0; cascade < CSM_CASCADE_COUNT; cascade++)
        {
          char label[32];
          snprintf(label, sizeof(label), "Cascade %u LOD", cascade);
          ImGui::SliderInt(label, &cascadeLods[cascade], 0, int(MeshSimplifier::LOD_COUNT) - 1);
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("0 is the source mesh. A mesh that could not be simplified to the\n"
            "requested level falls back to the nearest level below it.");
      }

      ImGui::SliderInt("Reflection Probe Bounces", &context.render->GetProbeBounceCount(),
        Render::MIN_PROBE_BOUNCES, Render::MAX_PROBE_BOUNCES);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Bake passes over every probe. Each extra pass lets probes\npick up the light their neighbours captured previously.");

      ImGui::SliderInt("Volume Bounces", &context.render->GetVolumeBounceCount(),
        Render::MIN_VOLUME_BOUNCES, Render::MAX_VOLUME_BOUNCES);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Bake passes over every irradiance volume in Bake All Volumes.\nEach extra pass lets a volume pick up the light its neighbours\ncaptured previously. Bake time grows linearly with the count.");

      ImGui::Separator();
      ImGui::Checkbox("Irradiance Volumes Enabled", &context.render->GetIrradianceVolumesEnabled());
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Off falls diffuse indirect back to skybox irradiance everywhere.");
      ImGui::DragFloat("Irradiance Normal Bias", &context.render->GetIrradianceNormalBias(),
        0.01f, 0.0f, 1.0f, "%.2f m");
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pushes the diffuse sample point along the surface normal.\nRaise it when light leaks through thin walls, lower it when\ncorners lose contact shadowing. Around half the node spacing.");

      if (ImGui::Button(ICON_FA_CIRCLE_PLAY " Bake All Reflection Probes"))
        context.render->BakeAllProbes(*context.scene, *context.assetManager);

      if (ImGui::Button(ICON_FA_CIRCLE_PLAY " Bake All Volumes"))
        context.render->BakeAllIrradianceVolumes(*context.scene, *context.assetManager);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Bakes every irradiance volume in the scene. Check the node count\non each volume first - bake time scales with it.");

      if (ImGui::Button(ICON_FA_ROTATE " Recompile Shaders"))
        context.render->GetShaderHotReload().RecompileAll();
    }

    ImGui::End();
  }
}
