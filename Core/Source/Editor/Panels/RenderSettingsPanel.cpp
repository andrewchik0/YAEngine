#include "Editor/Panels/RenderSettingsPanel.h"

#include <imgui.h>

#include "Assets/AssetManager.h"
#include "Editor/EditorCommands.h"
#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorIcons.h"
#include "Editor/Utils/EditorWidgets.h"
#include "Editor/Utils/FileDialog.h"
#include "Render/Render.h"
#include "Scene/Scene.h"

namespace YAEngine
{
  using namespace EditorWidgets;

  namespace
  {
    // Indices match GTAO_QUALITY_*
    constexpr EnumOption GTAO_QUALITY_LEVELS[] = {
      { .label = "Low", .tooltip = "1 slice x 2 steps per pixel" },
      { .label = "Medium", .tooltip = "2 slices x 2 steps per pixel" },
      { .label = "High", .tooltip = "3 slices x 3 steps per pixel" },
      { .label = "Ultra", .tooltip = "9 slices x 3 steps per pixel" },
    };

    // Indices match TONEMAP_ACES and TONEMAP_AGX
    constexpr EnumOption TONEMAPPERS[] = {
      { .label = "ACES", .tooltip = "Filmic curve with strong contrast; saturated highlights shift in hue" },
      { .label = "AgX", .tooltip = "Softer highlight roll-off; bright colors desaturate toward white" },
    };

    constexpr int32_t DEFAULT_CASCADE_LODS[CSM_CASCADE_COUNT] = { 0, 1, 1, 2 };

    const char* GetAntialiasingModeTooltip(AntialiasingMode mode)
    {
      switch (mode)
      {
        case AntialiasingMode::None: return "No temporal resolve; edges alias";
        case AntialiasingMode::TAA: return "The engine's temporal anti-aliasing at render resolution";
        case AntialiasingMode::DLAA: return "NVIDIA DLSS anti-aliasing at native resolution";
        default: return "NVIDIA DLSS renders at a lower resolution and upscales; Quality renders the most pixels, "
                        "Ultra Performance the fewest";
      }
    }

    std::string WithReason(const char* prefix, const std::string& reason)
    {
      return std::string(prefix) + (reason.empty() ? std::string("not supported on this device") : reason);
    }

    void DrawEnvironmentGroup(EditorContext& context, Render& render)
    {
      if (!BeginPropertyGroup("Environment", {
        .icon = ICON_LC_CLOUD_SUN, .defaultOpen = true, .tooltip = "Skybox and height fog" }))
      {
        return;
      }

      if (context.scene != nullptr && context.assetManager != nullptr)
      {
        Scene& scene = *context.scene;
        AssetManager& assets = *context.assetManager;
        const CubeMapHandle skybox = scene.GetSkybox();
        const std::string path = assets.CubeMaps().GetPath(skybox);
        const std::string relativePath = path.empty() ? std::string() : assets.MakeRelative(path);

        PropertyReadOnly("Skybox", relativePath.empty() ? "None" : relativePath.c_str(), {
          .mono = true,
          .tooltip = "HDR image the sky is drawn from. Its irradiance also lights surfaces no irradiance volume covers." });

        if (PropertyButton("Load Skybox...", {
          .icon = ICON_LC_FOLDER_OPEN, .tooltip = "Picks an .hdr image and makes it the scene skybox" }))
        {
          nfdu8filteritem_t filters[] = { { "HDR Images", "hdr" } };
          const std::string file = FileDialog::OpenFile(filters, 1);
          if (!file.empty())
            EditorCommands::LoadSkybox(scene, assets, file);
        }

        if (PropertyButton("Clear Skybox", {
          .icon = ICON_LC_X, .tooltip = "Removes the skybox from the scene",
          .disabledReason = skybox ? nullptr : "The scene has no skybox" }))
        {
          scene.SetSkybox({});
        }
      }

      bool& fog = render.GetFogEnabled();
      PropertyBool("Fog", fog, {
        .defaultValue = false,
        .tooltip = "Exponential height fog blended over lit surfaces, transparent surfaces and the sky" });

      PushDependency(fog, "Requires Fog");
      PropertyFloat("Fog Density", render.GetFogDensity(), {
        .min = 0.0f, .max = 1.0f, .speed = 0.0005f, .format = "%.4f", .defaultValue = 0.02f,
        .tooltip = "Fog extinction per meter at world height 0. The fog is denser below that height and thins above it." });
      PropertyFloat("Fog Height Falloff", render.GetFogHeightFalloff(), {
        .min = 0.001f, .max = 1.0f, .speed = 0.0005f, .format = "%.3f", .defaultValue = 0.1f,
        .tooltip = "How fast the density drops with height, per meter. Higher values keep the fog closer to the ground." });
      PropertyColor("Fog Color", render.GetFogColor(), {
        .defaultValue = glm::vec3(0.7f, 0.75f, 0.8f),
        .tooltip = "Color the fogged image fades toward" });
      PropertyFloat("Fog Max Opacity", render.GetFogMaxOpacity(), {
        .min = 0.0f, .max = 1.0f, .format = "%.2f", .slider = true, .defaultValue = 1.0f,
        .tooltip = "Normalized 0-1. Upper limit on how much fog any pixel receives." });
      PropertyFloat("Fog Start Distance", render.GetFogStartDistance(), {
        .min = 0.0f, .max = 1000.0f, .speed = 0.5f, .format = "%.1f", .unit = "m", .defaultValue = 10.0f,
        .tooltip = "Distance from the camera where the fog begins; nearer surfaces stay clear" });
      PopDependency();

      EndPropertyGroup();
    }

    void DrawLightingGroup(Render& render)
    {
      if (!BeginPropertyGroup("Lighting & GI", {
        .icon = ICON_LC_LIGHTBULB, .defaultOpen = false,
        .tooltip = "Ambient occlusion, screen-space diffuse GI and baked irradiance volumes" }))
      {
        return;
      }

      bool& gtao = render.GetAOEnabled();
      PropertyBool("GTAO", gtao, {
        .defaultValue = true,
        .tooltip = "Ground truth ambient occlusion (XeGTAO) from the depth buffer. Darkens ambient light in creases and "
                   "where objects meet." });

      PushDependency(gtao, "Requires GTAO");
      PropertyEnum("GTAO Quality", render.GetAOQualityLevel(), GTAO_QUALITY_LEVELS, {
        .defaultValue = GTAO_QUALITY_HIGH,
        .tooltip = "Directions (slices) and steps per slice sampled for each pixel. Higher levels are smoother and cost more." });
      PropertyBool("GTAO Denoise", render.GetAODenoiseEnabled(), {
        .defaultValue = true,
        .tooltip = "Edge-aware blur over the raw occlusion. Off shows the noisy per-pixel result." });
      PropertyFloat("GTAO Radius", render.GetAORadius(), {
        .min = 0.01f, .max = 20.0f, .speed = 0.01f, .format = "%.2f", .unit = "m", .defaultValue = 0.5f,
        .tooltip = "World-space radius around each pixel that occluders are searched in" });
      PropertyFloat("GTAO Specular Occlusion", render.GetAOSpecularStrength(), {
        .min = 0.0f, .max = 1.0f, .format = "%.2f", .slider = true, .defaultValue = 1.0f,
        .tooltip = "Normalized 0-1. How much the occlusion also darkens ambient reflections; 0 leaves them untouched." });
      PopDependency();

      PushDependency(gtao, "SSGI runs inside the GTAO pass");
      bool& ssgi = render.GetSSGIEnabled();
      PropertyBool("SSGI", ssgi, {
        .defaultValue = false,
        .tooltip = "Screen-space diffuse bounce gathered from the GTAO visibility bitmask. It runs inside the GTAO pass on the "
                   "same samples, so it needs GTAO on. While on, it replaces the diffuse occlusion approximation." });

      PushDependency(ssgi, "Requires SSGI");
      PropertyFloat("SSGI Radius", render.GetSSGIRadius(), {
        .min = 0.1f, .max = 20.0f, .speed = 0.05f, .format = "%.2f", .unit = "m", .defaultValue = 3.0f,
        .tooltip = "World-space gather radius of the bounce. Wider than GTAO Radius: the occlusion keeps its own falloff from "
                   "the same samples." });
      PropertyFloat("SSGI Thickness", render.GetSSGIThickness(), {
        .min = 0.01f, .max = 10.0f, .speed = 0.01f, .format = "%.2f", .unit = "m", .defaultValue = 0.3f,
        .tooltip = "Assumed occluder depth behind every screen sample. Thin geometry (railings, poles, foliage) stops "
                   "over-occluding as this goes down." });
      PropertyFloat("SSGI Intensity", render.GetSSGIIntensity(), {
        .min = 0.0f, .max = 4.0f, .speed = 0.01f, .format = "%.2f", .defaultValue = 1.0f,
        .tooltip = "Multiplier on the screen-gathered part only. The volume fallback is untouched, so 1 is the "
                   "energy-conserving value." });
      PopDependency();
      PopDependency();

      bool& volumes = render.GetIrradianceVolumesEnabled();
      PropertyBool("Use Irradiance Volumes", volumes, {
        .defaultValue = true,
        .tooltip = "Lights diffuse indirect from the baked irradiance volumes. Off falls back to skybox irradiance everywhere." });

      PushDependency(volumes, "Requires Use Irradiance Volumes");
      PropertyFloat("Irradiance Normal Bias", render.GetIrradianceNormalBias(), {
        .min = 0.0f, .max = 1.0f, .speed = 0.005f, .format = "%.2f", .unit = "m", .defaultValue = 0.25f,
        .tooltip = "Pushes the diffuse sample point along the surface normal. Raise it when light leaks through thin walls, "
                   "lower it when corners lose contact shadowing. Around half the brick spacing of the volume where the "
                   "leak shows." });
      PopDependency();

      EndPropertyGroup();
    }

    void DrawShadowsGroup(Render& render)
    {
      if (!BeginPropertyGroup("Shadows", {
        .icon = ICON_LC_MOON, .defaultOpen = false, .tooltip = "Shadow maps of the raster render path" }))
      {
        return;
      }

      bool& shadows = render.GetShadowsEnabled();
      PropertyBool("Shadows", shadows, {
        .defaultValue = true,
        .tooltip = "Shadow maps for the directional light cascades and for spot and point lights. Off renders every light "
                   "unshadowed. Path Tracing traces shadow rays instead of reading the maps." });

      PushDependency(shadows, "Requires Shadows");
      bool& cascadeLod = render.GetShadowLodEnabled();
      PropertyBool("Cascade LOD", cascadeLod, {
        .defaultValue = true,
        .tooltip = "Distant directional light cascades draw a simplified index stream over the same vertices. Off puts every "
                   "cascade back on the source mesh." });

      PushDependency(cascadeLod, "Requires Cascade LOD");
      int* cascadeLods = render.GetShadowCascadeLods();
      for (uint32_t cascade = 0; cascade < CSM_CASCADE_COUNT; cascade++)
      {
        char label[32];
        std::snprintf(label, sizeof(label), "Cascade %u LOD", cascade);
        char tooltip[256];
        std::snprintf(tooltip, sizeof(tooltip), "Mesh LOD the shadow casters of cascade %u draw, cascade 0 being the nearest "
          "to the camera. 0 is the source mesh; a mesh that could not be simplified that far uses the nearest level below.",
          cascade);

        PropertyInt(label, cascadeLods[cascade], {
          .min = 0, .max = int32_t(MeshSimplifier::LOD_COUNT) - 1, .speed = 0.05f,
          .defaultValue = DEFAULT_CASCADE_LODS[cascade], .tooltip = tooltip });
      }
      PopDependency();
      PopDependency();

      EndPropertyGroup();
    }

    void DrawReflectionsGroup(Render& render)
    {
      if (!BeginPropertyGroup("Reflections", {
        .icon = ICON_LC_FLIP_HORIZONTAL_2, .defaultOpen = false, .tooltip = "Screen-space reflections" }))
      {
        return;
      }

      bool& ssr = render.GetSSREnabled();
      PropertyBool("SSR", ssr, {
        .defaultValue = true,
        .tooltip = "Screen-space reflections traced against the depth buffer" });

      PushDependency(ssr, "Requires SSR");
      PropertyFloat("SSR Intensity", render.GetSSRIntensity(), {
        .min = 0.0f, .max = 20.0f, .speed = 0.05f, .format = "%.2f", .defaultValue = 1.0f,
        .tooltip = "Multiplier on the reflection mask. Fresnel keeps dielectric reflections near 4%, so values above 1 are the "
                   "usual way to make them readable." });
      PopDependency();

      EndPropertyGroup();
    }

    void DrawCameraGroup(Render& render)
    {
      if (!BeginPropertyGroup("Camera", {
        .icon = ICON_LC_APERTURE, .defaultOpen = false, .tooltip = "Exposure, tone mapping and display gamma" }))
      {
        return;
      }

      PropertyFloat("Exposure", render.GetExposure(), {
        .min = 0.01f, .max = 10.0f, .speed = 0.01f, .format = "%.2f", .defaultValue = 1.0f,
        .tooltip = "Multiplier on the scene radiance before tone mapping. With Auto Exposure on it scales the metered "
                   "exposure, as exposure compensation." });

      bool& autoExposure = render.GetAutoExposureEnabled();
      PropertyBool("Auto Exposure", autoExposure, {
        .defaultValue = true,
        .tooltip = "Meters the frame's luminance histogram and adapts the exposure toward middle grey over time" });

      PushDependency(autoExposure, "Requires Auto Exposure");
      PropertyFloat("Adaptation Speed Up", render.GetAdaptSpeedUp(), {
        .min = 0.1f, .max = 10.0f, .speed = 0.05f, .format = "%.2f", .unit = "1/s", .defaultValue = 2.0f,
        .tooltip = "Rate at which the exposure rises when the frame gets darker. Higher adapts faster." });
      PropertyFloat("Adaptation Speed Down", render.GetAdaptSpeedDown(), {
        .min = 0.1f, .max = 10.0f, .speed = 0.05f, .format = "%.2f", .unit = "1/s", .defaultValue = 1.0f,
        .tooltip = "Rate at which the exposure falls when the frame gets brighter. Higher adapts faster." });
      PropertyFloat("Low Percentile", render.GetLowPercentile(), {
        .min = 0.01f, .max = 0.5f, .speed = 0.005f, .format = "%.2f", .defaultValue = 0.1f,
        .tooltip = "Normalized 0-1. Share of the darkest pixels the meter ignores." });
      PropertyFloat("High Percentile", render.GetHighPercentile(), {
        .min = 0.5f, .max = 0.99f, .speed = 0.005f, .format = "%.2f", .defaultValue = 0.98f,
        .tooltip = "Normalized 0-1. The meter ignores pixels brighter than this share of the histogram, so small highlights "
                   "do not darken the frame." });
      PopDependency();

      PropertyEnum("Tonemapper", render.GetTonemapMode(), TONEMAPPERS, {
        .defaultValue = TONEMAP_AGX,
        .tooltip = "Curve that maps HDR radiance into the display range" });
      PropertyFloat("Gamma", render.GetGamma(), {
        .min = 0.5f, .max = 3.0f, .speed = 0.01f, .format = "%.2f", .defaultValue = 2.2f,
        .tooltip = "Display gamma the final image is encoded with. It also decodes albedo: base colors and textures are "
                   "raised to this power in the G-buffer, so changing it changes how materials look, not only the display "
                   "response." });

      EndPropertyGroup();
    }

    void DrawPostFxGroup(Render& render)
    {
      if (!BeginPropertyGroup("Post FX", {
        .icon = ICON_LC_SPARKLES, .defaultOpen = false, .tooltip = "Effects applied to the HDR image" }))
      {
        return;
      }

      bool& bloom = render.GetBloomEnabled();
      PropertyBool("Bloom", bloom, {
        .defaultValue = true,
        .tooltip = "Glow around bright pixels, built from a downsampled and blurred copy of the HDR image" });

      PushDependency(bloom, "Requires Bloom");
      PropertyFloat("Bloom Intensity", render.GetBloomIntensity(), {
        .min = 0.0f, .max = 1.0f, .speed = 0.001f, .format = "%.3f", .defaultValue = 0.04f,
        .tooltip = "Strength of the glow in the final image" });
      PropertyFloat("Bloom Threshold", render.GetBloomThreshold(), {
        .min = 0.0f, .max = 5.0f, .speed = 0.01f, .format = "%.2f", .defaultValue = 1.0f,
        .tooltip = "HDR luminance above which pixels start to glow" });
      PropertyFloat("Bloom Soft Knee", render.GetBloomSoftKnee(), {
        .min = 0.0f, .max = 1.0f, .format = "%.2f", .slider = true, .defaultValue = 0.5f,
        .tooltip = "Width of the smooth ramp around the threshold: the glow fades in from Threshold - Soft Knee to "
                   "Threshold + Soft Knee." });
      PopDependency();

      EndPropertyGroup();
    }

    void DrawAntialiasingGroup(Render& render)
    {
      if (!BeginPropertyGroup("Anti-aliasing & Upscaling", {
        .icon = ICON_LC_IMAGE_UPSCALE, .defaultOpen = false,
        .tooltip = "How the frame is resolved and, in the DLSS upscale modes, upscaled" }))
      {
        return;
      }

      const bool dlssAvailable = render.IsDLSSAvailable();
      const std::string dlssUnavailable = WithReason("DLSS is unavailable: ", render.GetDLSSUnavailableReason());

      std::array<EnumOption, size_t(AntialiasingMode::Count)> modes;
      for (size_t i = 0; i < modes.size(); i++)
      {
        const AntialiasingMode option = AntialiasingMode(i);
        modes[i] = EnumOption {
          .label = GetAntialiasingModeName(option),
          .disabledReason = IsDLSSMode(option) && !dlssAvailable ? dlssUnavailable.c_str() : nullptr,
          .tooltip = GetAntialiasingModeTooltip(option),
        };
      }

      AntialiasingMode& mode = render.GetAntialiasingMode();
      PropertyEnum("Mode", mode, modes, {
        .defaultValue = int32_t(AntialiasingMode::TAA),
        .tooltip = "Resolve that anti-aliases the frame. The DLSS modes run NVIDIA DLSS; all of them except DLAA render at a "
                   "lower resolution and upscale." });

      if (!dlssAvailable)
        PropertyStatus(nullptr, dlssUnavailable.c_str(), StatusKind::Warning);

      // Under the path tracing render path ray reconstruction resolves the frame and only exists
      // inside the DLSS family, so a non-DLSS selection runs as DLAA for the duration
      if (render.IsPathTracingActive())
      {
        const AntialiasingMode effective = render.GetEffectiveAntialiasingMode();
        if (render.IsPathTraceDevResolveEnabled())
        {
          PropertyStatus(nullptr, "Developer Resolve: the mode only drives the camera jitter", StatusKind::Info);
        }
        else if (effective != mode)
        {
          const std::string text = std::string("Path Tracing resolves through Ray Reconstruction, running as ")
            + GetAntialiasingModeName(effective);
          PropertyStatus(nullptr, text.c_str(), StatusKind::Info);
        }
        else
        {
          PropertyStatus(nullptr, "Path Tracing resolves through Ray Reconstruction", StatusKind::Info);
        }
      }

      // Only the upscale modes make the two differ
      const VkExtent2D renderExtent = render.GetRenderExtent();
      const VkExtent2D outputExtent = render.GetOutputExtent();
      if (renderExtent.width != outputExtent.width || renderExtent.height != outputExtent.height)
      {
        char resolution[64];
        std::snprintf(resolution, sizeof(resolution), "%ux%u -> %ux%u",
          renderExtent.width, renderExtent.height, outputExtent.width, outputExtent.height);
        PropertyReadOnly("Resolution", resolution, {
          .mono = true,
          .tooltip = "Render resolution the scene is shaded at, and the output resolution the mode upscales to" });
      }

      EndPropertyGroup();
    }

    void DrawRenderPathGroup(Render& render)
    {
      if (!BeginPropertyGroup("Render Path", {
        .icon = ICON_LC_ROUTE, .defaultOpen = false, .tooltip = "Raster or path traced shading, and the path tracer's settings" }))
      {
        return;
      }

      std::array<EnumOption, size_t(RenderPath::Count)> paths;
      for (size_t i = 0; i < paths.size(); i++)
      {
        const RenderPath option = RenderPath(i);
        paths[i] = EnumOption {
          .label = GetRenderPathName(option),
          .disabledReason = render.GetRenderPathUnavailableReason(option),
          .tooltip = option == RenderPath::PathTracing
            ? "Hardware path tracer: one sample per pixel, resolved by DLSS Ray Reconstruction"
            : "Deferred PBR rasterization with the screen-space effects and baked GI",
        };
      }

      RenderPath& path = render.GetRenderPath();
      PropertyEnum("Path", path, paths, {
        .defaultValue = int32_t(RenderPath::Raster),
        .tooltip = "Which pipeline shades the frame. Path Tracing keeps the raster depth, G-buffer and velocity and "
                   "replaces the lighting and the resolve." });

      const RenderPath effectivePath = render.GetEffectiveRenderPath();
      if (effectivePath != path)
      {
        const std::string text = std::string("Falling back to ") + GetRenderPathName(effectivePath);
        PropertyStatus(nullptr, text.c_str(), StatusKind::Warning, render.GetRenderPathUnavailableReason(path));
      }

      // Path traced debug views in raster mode are driven by the same two settings
      const bool pathTracerAvailable = render.IsPathTracerAvailable();
      const bool pathTracerInUse = effectivePath == RenderPath::PathTracing || render.IsPathTraceView();
      PushDependency(pathTracerAvailable && pathTracerInUse, pathTracerAvailable
        ? "Requires the Path Tracing render path or a path traced debug view"
        : "The path tracer is unavailable on this device");
      PropertyInt("PT Bounces", render.GetPathTraceMaxBounces(), {
        .min = PT_MIN_BOUNCES, .max = PT_MAX_BOUNCES, .speed = 0.05f, .defaultValue = 3,
        .tooltip = "Path vertices after the G-buffer one. Every bounce costs a full trace plus a shadow ray, and the image "
                   "needs proportionally more samples to converge." });
      PropertyFloat("PT Firefly Clamp", render.GetPathTraceFireflyClamp(), {
        .min = PT_MIN_FIREFLY_CLAMP, .max = PT_MAX_FIREFLY_CLAMP, .speed = 0.1f, .format = "%.1f", .defaultValue = 10.0f,
        .tooltip = "Ceiling on the radiance one bounce may add. 0 switches it off, which is the unbiased setting - and the "
                   "one to compare against when a converged image looks too dark." });
      PopDependency();

      if (!render.IsRayReconstructionAvailable())
      {
        const std::string text = WithReason("Ray Reconstruction is unavailable: ",
          render.GetRayReconstructionUnavailableReason());
        PropertyStatus(nullptr, text.c_str(), StatusKind::Warning);
      }

      EndPropertyGroup();
    }

    void DrawBakingGroup(EditorContext& context, Render& render)
    {
      if (!BeginPropertyGroup("Baking", {
        .icon = ICON_LC_COOKING_POT, .defaultOpen = false,
        .tooltip = "Scene-wide bake settings and Bake All. Single probes and volumes bake from the Details panel." }))
      {
        return;
      }

      PropertyInt("Reflection Probe Bounces", render.GetProbeBounceCount(), {
        .min = Render::MIN_PROBE_BOUNCES, .max = Render::MAX_PROBE_BOUNCES, .speed = 0.05f, .defaultValue = 1,
        .tooltip = "Passes over every probe in Bake All Reflection Probes. Each extra pass lets probes pick up the light "
                   "their neighbours captured in the previous one." });
      PropertyInt("Volume Bounces", render.GetVolumeBounceCount(), {
        .min = Render::MIN_VOLUME_BOUNCES, .max = Render::MAX_VOLUME_BOUNCES, .speed = 0.05f, .defaultValue = 3,
        .tooltip = "Path bounces per sample of the ray traced irradiance volume bake, as PT Bounces is for the path tracer. "
                   "Every bounce adds a trace and a shadow ray to each sample." });
      PropertyInt("Volume Samples", render.GetVolumeSampleCount(), {
        .min = Render::MIN_VOLUME_SAMPLES, .max = Render::MAX_VOLUME_SAMPLES, .speed = 4.0f,
        .defaultValue = int32_t(BakeLimits::VOLUME_DEFAULT_SAMPLES_PER_PROBE),
        .tooltip = "Paths traced per irradiance volume node. Noise falls with the square root of the count, bake time grows "
                   "linearly with it." });
      PropertyFloat("Volume Firefly Clamp", render.GetVolumeFireflyClamp(), {
        .min = PT_MIN_FIREFLY_CLAMP, .max = PT_MAX_FIREFLY_CLAMP, .speed = 0.1f, .format = "%.1f", .defaultValue = 10.0f,
        .tooltip = "Ceiling on the radiance one bounce may add in the volume bake. The default 10 keeps small, very bright "
                   "emitters near a node from baking a colored blob without darkening the result; 2 already biases dark. "
                   "0 switches it off, which is the unbiased setting." });

      if (context.scene != nullptr && context.assetManager != nullptr)
      {
        if (PropertyButton("Bake All Reflection Probes", {
          .icon = ICON_LC_CIRCLE_PLAY,
          .tooltip = "Bakes every reflection probe of the scene, Reflection Probe Bounces passes over all of them. The "
                     "editor stalls until the bake ends." }))
        {
          render.BakeAllProbes(*context.scene, *context.assetManager);
        }

        const bool volumeBakeAvailable = render.IsRayTracedBakeAvailable();
        if (PropertyButton("Bake All Volumes", {
          .icon = ICON_LC_CIRCLE_PLAY,
          .tooltip = "Bakes every irradiance volume of the scene by ray tracing. Preview Placement on each volume shows its "
                     "node count first - bake time scales with nodes x Volume Samples.",
          .disabledReason = volumeBakeAvailable ? nullptr
            : "Irradiance volumes bake by ray tracing, and the ray traced baker is unavailable: no hardware ray tracing "
              "pipeline or no bindless texture table." }))
        {
          render.BakeAllIrradianceVolumes(*context.scene, *context.assetManager);
        }
      }

      EndPropertyGroup();
    }
  }

  void RenderSettingsPanel::OnRender(EditorContext& context)
  {
    if (BeginPanel())
    {
      if (context.render == nullptr)
      {
        ImGui::TextDisabled("Render not available");
      }
      else
      {
        Render& render = *context.render;
        DrawEnvironmentGroup(context, render);
        DrawLightingGroup(render);
        DrawShadowsGroup(render);
        DrawReflectionsGroup(render);
        DrawCameraGroup(render);
        DrawPostFxGroup(render);
        DrawAntialiasingGroup(render);
        DrawRenderPathGroup(render);
        DrawBakingGroup(context, render);
      }
    }
    ImGui::End();
  }
}
