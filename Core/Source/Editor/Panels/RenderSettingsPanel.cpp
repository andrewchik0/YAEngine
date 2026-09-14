#include "Editor/Panels/RenderSettingsPanel.h"

#include <imgui.h>

#include "Editor/Bridge/BridgeTypes.h"
#include "Editor/EditorCommands.h"
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
          EditorCommands::LoadSkybox(scene, assets, path);
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
        "Direct Only", "Ray Query", "RT Pipeline",
        "PT Noisy", "PT Reference", "PT Guides",
        "PT Max Contribution", "PT NEE", "PT Environment", "PT Non-Finite",
        "HDR Magnitude", "PT Specular Motion", "Volume Level"
      };

      // Spelled out rather than an ImGui::Combo because several entries need device
      // capabilities or a render path, which only the per-item form can disable.
      if (ImGui::BeginCombo("Debug View", debugViews[debugViewIndex]))
      {
        for (int i = 0; i < IM_ARRAYSIZE(debugViews); i++)
        {
          const char* unavailableReason = EditorCommands::GetDebugViewUnavailableReason(*context.render, i);
          bool selectable = unavailableReason == nullptr;

          ImGui::BeginDisabled(!selectable);
          if (ImGui::Selectable(debugViews[i], i == debugViewIndex) && selectable)
            context.render->SetDebugView(i);
          ImGui::EndDisabled();

          if (unavailableReason != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", unavailableReason);
        }
        ImGui::EndCombo();
      }
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

      // Which pipeline shades the frame. Above the anti-aliasing combo because it decides
      // what there is to anti-alias: the path tracer resolves itself and switches the
      // engine's own resolve off.
      {
        RenderPath& path = context.render->GetRenderPath();
        RenderPath effectivePath = context.render->GetEffectiveRenderPath();
        bool pathTracerAvailable = context.render->IsPathTracerAvailable();

        if (ImGui::BeginCombo("Render Path", GetRenderPathName(path)))
        {
          for (uint32_t i = 0; i < uint32_t(RenderPath::Count); i++)
          {
            RenderPath option = RenderPath(i);
            const char* reason = context.render->GetRenderPathUnavailableReason(option);

            ImGui::BeginDisabled(reason != nullptr);
            if (ImGui::Selectable(GetRenderPathName(option), option == path) && reason == nullptr)
              path = option;
            ImGui::EndDisabled();

            if (reason != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
              ImGui::SetTooltip("%s", reason);
          }
          ImGui::EndCombo();
        }

        if (effectivePath != path)
          ImGui::TextDisabled("Falling back to %s", GetRenderPathName(effectivePath));

        // Ray reconstruction is what turns the tracer's single sample into a frame, so its
        // status belongs next to the combo that selects the path, exactly the way the DLSS
        // status sits under the anti-aliasing one.
        if (!context.render->IsRayReconstructionAvailable())
        {
          const std::string& rrReason = context.render->GetRayReconstructionUnavailableReason();
          ImGui::TextDisabled("Ray Reconstruction is unavailable: %s",
            rrReason.empty() ? "not supported on this device" : rrReason.c_str());
        }

        // Developer only, and deliberately not serialized: it resolves the path tracer out
        // of its own running mean instead of the denoiser, which is the only way to look at
        // the raw sample and at the converged reference while the render path owns the
        // frame. Shown whatever the path is currently set to, because on a device without
        // ray reconstruction ticking it is what makes the Path Tracing entry selectable at
        // all - it only disappears where there is no tracer to resolve either way.
        if (pathTracerAvailable)
        {
          bool devResolve = context.render->IsPathTraceDevResolveEnabled();
          if (ImGui::Checkbox("PT Developer Resolve (no denoiser)", &devResolve))
            context.render->SetPathTraceDevResolve(devResolve);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Resolves the path tracer out of its accumulated mean instead of\n"
              "DLSS Ray Reconstruction: one noisy sample while anything moves,\n"
              "converging to the reference while nothing does. Traces 1:1 and is\n"
              "not saved with the scene.");
        }

        // The tracer's own settings, next to the combo that turns it on - and still
        // reachable in raster mode while one of the path traced debug views is up, which
        // is the other thing they drive.
        if (pathTracerAvailable
          && (effectivePath == RenderPath::PathTracing || context.render->IsPathTraceView()))
        {
          ImGui::SliderInt("PT Bounces", &context.render->GetPathTraceMaxBounces(),
            PT_MIN_BOUNCES, PT_MAX_BOUNCES);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Path vertices after the G-buffer one. Every bounce costs a full\n"
              "trace plus a shadow ray, and the image needs proportionally more\n"
              "samples to converge.");

          // AlwaysClamp: without it Ctrl+Click text entry ignores the range, and a value typed
          // past the ceiling is exactly what the scene serializer would silently pull back.
          ImGui::DragFloat("PT Firefly Clamp", &context.render->GetPathTraceFireflyClamp(),
            0.1f, PT_MIN_FIREFLY_CLAMP, PT_MAX_FIREFLY_CLAMP, "%.1f",
            ImGuiSliderFlags_AlwaysClamp);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Ceiling on the radiance one bounce may add. 0 switches it off,\n"
              "which is the unbiased setting - and the one to compare against\n"
              "when a converged image looks too dark.");

          // The running mean is what the render path presents, and what the reference
          // view displays; the noisy view alone never touches it.
          if (effectivePath == RenderPath::PathTracing
            || context.render->GetDebugView() == DEBUG_VIEW_PT_REFERENCE)
          {
            ImGui::Text("Accumulated samples: %d", context.render->GetPathTraceSampleCount() + 1);
            if (ImGui::Button(ICON_FA_ROTATE " Reset Accumulation"))
              context.render->ResetPathTraceAccumulation();
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("The mean also restarts on its own whenever the camera moves,\n"
                "the scene or the lights change, or the render resolution does.");
          }
        }
      }

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

        // Under the path tracing render path the effective mode is not the selected one:
        // ray reconstruction resolves the frame and only exists inside the DLSS family, so
        // a non-DLSS selection is promoted to DLAA for the duration and put back untouched
        // the moment the path goes away. With the developer resolve the mode drives nothing
        // but the camera jitter, which is what gives the accumulated mean its edges.
        if (context.render->IsPathTracingActive())
        {
          if (context.render->IsPathTraceDevResolveEnabled())
            ImGui::TextDisabled("Developer resolve: the mode only drives camera jitter.");
          else if (effective != mode)
            ImGui::TextDisabled("Path Tracing resolves through Ray Reconstruction, running as %s.",
              GetAntialiasingModeName(effective));
          else
            ImGui::TextDisabled("Path Tracing resolves through Ray Reconstruction.");
        }

        // Only the upscale modes make the two differ; showing it always would just be
        // the viewport size printed twice.
        VkExtent2D renderExtent = context.render->GetRenderExtent();
        VkExtent2D outputExtent = context.render->GetOutputExtent();
        if (renderExtent.width != outputExtent.width || renderExtent.height != outputExtent.height)
          ImGui::Text("Render %ux%u -> output %ux%u",
            renderExtent.width, renderExtent.height, outputExtent.width, outputExtent.height);

        if (UsesTAAPass(effective) && !context.render->IsPathTracingActive())
          ImGui::DragFloat("TAA Clamp Sigma", &context.render->GetTAAClampSigma(), 0.01f, 0.0f, 8.0f);

        // Only reachable while ray reconstruction is the resolve that produced the image on
        // screen. Not serialized: section 3.13 of the DLSS-RR Integration Guide recommends
        // shipping the default and offering the named presets for experimentation only.
        if (context.render->IsPathTracingActive()
          && !context.render->IsPathTraceDevResolveEnabled()
          && ImGui::CollapsingHeader("Ray Reconstruction"))
        {
          RayReconstructionSettings& rr = context.render->GetRayReconstructionSettings();

          if (ImGui::BeginCombo("DLSSDPreset", GetRayReconstructionPresetName(rr.preset)))
          {
            for (uint32_t i = 0; i < uint32_t(RayReconstructionPreset::Count); i++)
            {
              RayReconstructionPreset option = RayReconstructionPreset(i);
              if (ImGui::Selectable(GetRayReconstructionPresetName(option), option == rr.preset))
                rr.preset = option;
            }
            ImGui::EndCombo();
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("eDefault is what NVIDIA recommends shipping - it resolves to\n"
              "whatever the installed nvngx_dlssd.dll considers current. The named\n"
              "presets are for experimentation and pin an older network.");

          if (ImGui::BeginCombo("Specular Guide", GetRayReconstructionSpecularGuideName(rr.specularGuide)))
          {
            for (uint32_t i = 0; i < uint32_t(RayReconstructionSpecularGuide::Count); i++)
            {
              RayReconstructionSpecularGuide option = RayReconstructionSpecularGuide(i);
              if (ImGui::Selectable(GetRayReconstructionSpecularGuideName(option), option == rr.specularGuide))
                rr.specularGuide = option;
            }
            ImGui::EndCombo();
          }
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Which specular guide RR is tagged with - never both. Motion vectors\n"
              "are computed by the tracer and cover a moving reflector and a moving\n"
              "reflected object; from the hit distance RR builds its own from the\n"
              "camera alone. Developer toggle, not saved with the scene.");

          ImGui::TextDisabled("Changing the preset or the specular guide restarts the RR history.");
        }
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

      // AlwaysClamp on the three volume bake settings: Ctrl+Click text entry otherwise ignores
      // the range the scene serializer clamps to.
      ImGui::SliderInt("Volume Bounces", &context.render->GetVolumeBounceCount(),
        Render::MIN_VOLUME_BOUNCES, Render::MAX_VOLUME_BOUNCES, "%d", ImGuiSliderFlags_AlwaysClamp);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Path bounces per sample of the ray traced irradiance volume bake,\n"
          "as PT Bounces is for the path tracer. Every bounce adds a trace and\n"
          "a shadow ray to each sample.");

      ImGui::SliderInt("Volume Samples", &context.render->GetVolumeSampleCount(),
        Render::MIN_VOLUME_SAMPLES, Render::MAX_VOLUME_SAMPLES, "%d",
        ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Paths traced per irradiance volume node. Noise falls with the\n"
          "square root of the count, bake time grows linearly with it.");

      ImGui::DragFloat("Volume Firefly Clamp", &context.render->GetVolumeFireflyClamp(),
        0.1f, PT_MIN_FIREFLY_CLAMP, PT_MAX_FIREFLY_CLAMP, "%.1f", ImGuiSliderFlags_AlwaysClamp);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ceiling on the radiance one bounce may add in the volume bake.\n"
          "The default 10 keeps small, very bright emitters near a node from baking a\n"
          "colored blob without darkening the result; 2 already biases dark.\n"
          "0 switches it off, which is the unbiased setting.");

      ImGui::Separator();
      ImGui::Checkbox("Irradiance Volumes Enabled", &context.render->GetIrradianceVolumesEnabled());
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Off falls diffuse indirect back to skybox irradiance everywhere.");
      ImGui::DragFloat("Irradiance Normal Bias", &context.render->GetIrradianceNormalBias(),
        0.01f, 0.0f, 1.0f, "%.2f m");
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pushes the diffuse sample point along the surface normal.\nRaise it when light leaks through thin walls, lower it when\ncorners lose contact shadowing. Around half the brick spacing\nof the volume where the leak shows.");

      if (ImGui::Button(ICON_FA_CIRCLE_PLAY " Bake All Reflection Probes"))
        context.render->BakeAllProbes(*context.scene, *context.assetManager);

      const bool volumeBakeAvailable = context.render->IsRayTracedBakeAvailable();
      ImGui::BeginDisabled(!volumeBakeAvailable);
      if (ImGui::Button(ICON_FA_CIRCLE_PLAY " Bake All Volumes"))
        context.render->BakeAllIrradianceVolumes(*context.scene, *context.assetManager);
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
        if (volumeBakeAvailable)
          ImGui::SetTooltip("Bakes every irradiance volume in the scene by ray tracing. Preview Placement\n"
            "on each volume shows its node count first - bake time scales with nodes x Volume Samples.");
        else
          ImGui::SetTooltip("Irradiance volumes bake by ray tracing, and the ray traced baker is\n"
            "unavailable: no hardware ray tracing pipeline or no bindless texture table.");
      }

      if (ImGui::Button(ICON_FA_ROTATE " Recompile Shaders"))
        context.render->GetShaderHotReload().RecompileAll();

      // Render services one capture request at a time, and a shot takes whatever result comes back
      // as its own, so a click must not slip a frame into running capture work.
      const char* captureBlocked = nullptr;
      if (context.render->IsCaptureRequestPending())
        captureBlocked = "A capture request is still waiting for its frame.";
      else if (context.captureSession != nullptr && context.captureSession->running)
        captureBlocked = "A --capture session is running.";
      else if (context.bridgeCapture != nullptr && context.bridgeCapture->shotRunning)
        captureBlocked = "An agent capture shot is running; it restores the view when it ends.";

      // Secondary to the --capture command line, which is what an unattended agent uses.
      // Deliberately without settings of its own: FrameCapture introduces no scene state.
      ImGui::BeginDisabled(captureBlocked != nullptr);
      bool captureClicked = ImGui::Button(ICON_FA_CAMERA " Capture Frame");
      ImGui::EndDisabled();
      if (captureClicked)
      {
        static int captureIndex = 0;
        char directory[128];
        std::snprintf(directory, sizeof(directory), "Captures/%03d_manual", captureIndex++);
        context.render->RequestCapture(FrameCaptureRequest {
          .directory = directory,
          .targets = { "final", "resolved" },
          .shotName = "manual",
          .requestedBy = "editor button",
          .scenePath = context.scene ? context.scene->GetScenePath() : std::string()
        });
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
        if (captureBlocked != nullptr)
          ImGui::SetTooltip("%s", captureBlocked);
        else
          ImGui::SetTooltip("Dumps the current frame into Captures/NNN_manual next to the\n"
            "executable, in the same layout a --capture run produces.");
      }
    }

    ImGui::End();
  }
}
