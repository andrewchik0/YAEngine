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
        "Reflection Probe Index", "Reflection Probe Fallback", "Volume Coverage"
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
        ImGui::DragFloat("AO Strength", &context.render->GetAOStrength(), 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("AO Specular Strength", &context.render->GetAOSpecularStrength(), 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("AO Multi Bounce", &context.render->GetAOMultiBounce(), 0.01f, 0.0f, 1.0f);

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
      ImGui::Checkbox("SSR", &context.render->GetSSREnabled());
      if (context.render->GetSSREnabled())
        ImGui::DragFloat("SSR Intensity", &context.render->GetSSRIntensity(), 0.05f, 0.0f, 20.0f);
      ImGui::Checkbox("TAA", &context.render->GetTAAEnabled());
      if (context.render->GetTAAEnabled())
        ImGui::DragFloat("TAA Clamp Sigma", &context.render->GetTAAClampSigma(), 0.01f, 0.0f, 8.0f);

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

      ImGui::Separator();
      ImGui::Checkbox("Indirect Shadows", &context.render->GetShadowIndirectEnabled());
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Batches opaque shadow casters into one indirect draw per atlas tile\n"
          "per cull mode. Off falls back to the legacy one-draw-per-caster path.\n"
          "Both must render identically - watch Shadow draws in the performance panel.");

      if (context.render->GetShadowIndirectEnabled())
      {
        ImGui::Checkbox("Quantized Positions", &context.render->GetShadowQuantizedPositionsEnabled());
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Indirect casters fetch 8-byte quantized positions instead of 12-byte ones,\n"
            "restored by a transform folded into the model matrix.\n"
            "Only the shadow atlas reads them - the depth prepass keeps the exact stream.\n"
            "Watch for contact shadows drifting and acne on large meshes.");
      }

      // Measurement spike for the shadow caching project - deliberately not
      // serialized so it resets to the same value on every launch (see ShadowClearMode).
      int shadowClearMode = int(context.render->GetShadowClearMode());
      // Indices must match ShadowClearMode in Render.h
      const char* shadowClearModes[] = { "Full Clear", "Per-Tile Passes", "Load + Clear Rects" };
      if (ImGui::Combo("Shadow Clear Mode", &shadowClearMode, shadowClearModes, IM_ARRAYSIZE(shadowClearModes)))
        context.render->GetShadowClearMode() = ShadowClearMode(shadowClearMode);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Measurement spike for the shadow caching project.\n"
          "Full Clear: one pass over the whole atlas with LOAD_OP_CLEAR - the\n"
          "fast-clear-friendly control group, no longer the default.\n"
          "Per-Tile Passes: one render pass instance per atlas tile, clearing only that\n"
          "tile's rect and preserving the rest of the atlas.\n"
          "Load + Clear Rects (default): one LOAD_OP_LOAD pass over the atlas, with a\n"
          "vkCmdClearAttachments rect before each tile's draws.\n"
          "Bakes always use Full Clear. Not serialized - resets on launch.");

      ImGui::Checkbox("Cascade Fit Hysteresis", &context.render->GetShadowFitHysteresisEnabled());
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Freezes each cascade's light matrix until its camera frustum slice no\n"
          "longer fits inside the frozen sphere, then refits once with the margin.\n"
          "Shadows look near-identical: the map stops re-centering on small camera\n"
          "moves, at up to the margin factor of effective texel density.\n"
          "Frozen matrices stay bit-identical - the future tile cache keys on that.\n"
          "Off restores the exact per-frame fit (A/B control).\n"
          "Watch the cascade refits block in the performance panel.");

      if (context.render->GetShadowFitHysteresisEnabled())
      {
        ImGui::SliderFloat("Fit Margin", &context.render->GetShadowFitMargin(), 1.0f, 1.5f, "%.2f");
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Sphere inflation applied on every refit. Larger survives more camera\n"
            "motion between refits but costs texel density by the same factor.");

        ImGui::SliderFloat("Sun Threshold (deg)", &context.render->GetShadowSunThresholdDeg(),
          0.0f, 5.0f, "%.2f");
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("How far the sun may drift from the frozen direction before every\n"
            "cascade refits. Inside the threshold shadows lag the sun by up to\n"
            "this angle - keep it small when the sun animates.");

        ImGui::SliderInt("Refit Budget", &context.render->GetShadowRefitBudget(), 0, 4);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Stage 5: proactive cascade refits per frame. A cascade close to\n"
            "escaping its frozen sphere refits early (highest urgency first),\n"
            "so cascades stop escaping together in one expensive frame. Hard\n"
            "escapes still refit immediately and consume the budget.\n"
            "0 disables scheduling - the exact stage 4 behavior (A/B control).");

        ImGui::SliderFloat("Refit Threshold", &context.render->GetShadowRefitThreshold(),
          0.90f, 1.00f, "%.2f");
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Fit urgency at which a cascade becomes a proactive refit candidate\n"
            "(1.0 = the escape boundary). Lower schedules earlier and spreads\n"
            "refits more evenly. Clamped internally above 1/margin so a fresh\n"
            "refit can never immediately re-enqueue itself.");
      }

      bool fitHysteresisOn = context.render->GetShadowFitHysteresisEnabled();
      ImGui::BeginDisabled(!fitHysteresisOn);
      ImGui::Checkbox("Shadow Caching", &context.render->GetShadowCacheEnabled());
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Stage 3 of the shadow caching project, all-or-nothing granularity:\n"
          "skips the entire shadow pass while nothing shadow-relevant changed\n"
          "since the last rendered frame - the atlas keeps its content.\n"
          "Requires Cascade Fit Hysteresis: without frozen fits every frame\n"
          "refits and there is nothing to reuse.\n"
          "Watch the Shadow cache line in the performance panel. A shadow that\n"
          "sticks while its caster moves is an invalidation bug, not a feature.");
      if (!fitHysteresisOn)
        ImGui::TextDisabled("Shadow Caching needs Cascade Fit Hysteresis");

      bool shadowCacheOn = fitHysteresisOn && context.render->GetShadowCacheEnabled();
      ImGui::BeginDisabled(!shadowCacheOn);
      ImGui::Checkbox("Per-Tile Rebuilds", &context.render->GetShadowCachePerTileEnabled());
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Stage 4 of the shadow caching project: when the only change is a\n"
          "cascade refit, only the refitted cascade tiles are redrawn and the\n"
          "rest of the atlas keeps its content. Digest-driven invalidations\n"
          "(lights, settings, geometry, bakes) still rebuild the whole atlas;\n"
          "caster movement is handled by Dirty Rect Updates below when it is\n"
          "enabled. Off restores the stage 3 all-or-nothing behavior (A/B).\n"
          "Watch the Shadow cache PARTIAL line and the Shadow tiles rows in\n"
          "the performance panel.");

      bool perTileOn = shadowCacheOn && context.render->GetShadowCachePerTileEnabled();
      ImGui::BeginDisabled(!perTileOn);
      ImGui::Checkbox("Dirty Rect Updates", &context.render->GetShadowCacheDirtyRectEnabled());
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Stage 6 of the shadow caching project: a moving caster patches only\n"
          "the atlas regions its projected footprint touches - per cascade\n"
          "tile a padded rect over the union of its previous and current\n"
          "bounds, spot/point tiles wholesale when a mover intersects their\n"
          "frustum. Off makes any caster movement a full atlas rebuild again\n"
          "(stage 5 behavior, A/B control). Watch the Shadow cache RECT line\n"
          "and the rect rows under Shadow tiles in the performance panel.");

      ImGui::Checkbox("Shadow Cascade LOD", &context.render->GetShadowLodEnabled());
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Distant cascades draw a simplified index stream over the same vertices.\n"
          "Off puts every cascade back on the source mesh.\n"
          "Watch Shadow tris and Shadow LOD saved in the performance panel.");

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
