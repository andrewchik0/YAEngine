#include "Editor/Panels/DeveloperPanel.h"

#include "Assets/AssetManager.h"
#include "Editor/EditorCommands.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorPreferences.h"
#include "Editor/Utils/EditorFonts.h"
#include "Editor/Utils/EditorIcons.h"
#include "Editor/Utils/EditorStyle.h"
#include "Editor/Utils/EditorWidgets.h"
#include "Render/BakeLimits.h"
#include "Render/Render.h"
#include "Scene/Components.h"
#include "Utils/IrradianceGrid.h"

namespace YAEngine
{
  using namespace EditorWidgets;

  const DeveloperPanel::Group DeveloperPanel::GROUPS[] = {
    {
      .label = "GTAO",
      .icon = ICON_LC_SUN_DIM,
      .tooltip = "Fitted GTAO constants and the occlusion terms SSGI replaces. Saved with the scene.",
      .draw = &DeveloperPanel::DrawGtaoGroup,
    },
    {
      .label = "TAA",
      .icon = ICON_LC_HISTORY,
      .tooltip = "The engine's temporal resolve. Saved with the scene.",
      .draw = &DeveloperPanel::DrawTaaGroup,
    },
    {
      .label = "Path Tracer",
      .icon = ICON_LC_ORBIT,
      .tooltip = "Denoiser-free resolve and the running mean of the path tracer. Not saved with the scene.",
      .draw = &DeveloperPanel::DrawPathTracerGroup,
    },
    {
      .label = "DLSS Ray Reconstruction",
      .icon = ICON_LC_WAND_SPARKLES,
      .tooltip = "Ray Reconstruction experiments. Not saved with the scene.",
      .draw = &DeveloperPanel::DrawRayReconstructionGroup,
    },
    {
      .label = "Irradiance Volume Diagnostics",
      .icon = ICON_LC_BOXES,
      .tooltip = "Placement preview of the selected irradiance volume: bricks and nodes per spacing level, estimates, "
                 "validation and timings",
      .draw = &DeveloperPanel::DrawIrradianceVolumeDiagnosticsGroup,
    },
    {
      .label = "Reflection Probe Diagnostics",
      .icon = ICON_LC_GLOBE,
      .tooltip = "Prefiltered cubemap faces of the selected baked reflection probe",
      .draw = &DeveloperPanel::DrawReflectionProbeDiagnosticsGroup,
    },
    {
      .label = "Theme",
      .icon = ICON_LC_PALETTE,
      .tooltip = "Live editor for the theme tokens the whole editor is drawn with",
      .draw = &DeveloperPanel::DrawThemeGroup,
    },
    {
      .label = "ImGui Tools",
      .icon = ICON_LC_WRENCH,
      .tooltip = "Dear ImGui's built-in debugging windows",
      .draw = &DeveloperPanel::DrawImGuiToolsGroup,
    },
  };

  namespace
  {
    // Larger values are legal in editor.yaml; the drag only stops here
    constexpr float MAX_METRIC_PIXELS = 32.0f;

    constexpr EnumOption RR_PRESETS[] = {
      { .label = GetRayReconstructionPresetName(RayReconstructionPreset::Default),
        .tooltip = "Whatever the installed nvngx_dlssd.dll considers current; what NVIDIA recommends shipping" },
      { .label = GetRayReconstructionPresetName(RayReconstructionPreset::D), .tooltip = "Pins the default transformer model" },
      { .label = GetRayReconstructionPresetName(RayReconstructionPreset::E), .tooltip = "Pins the latest transformer model" },
      { .label = GetRayReconstructionPresetName(RayReconstructionPreset::F) },
      { .label = GetRayReconstructionPresetName(RayReconstructionPreset::G) },
      { .label = GetRayReconstructionPresetName(RayReconstructionPreset::H) },
      { .label = GetRayReconstructionPresetName(RayReconstructionPreset::I) },
      { .label = GetRayReconstructionPresetName(RayReconstructionPreset::J) },
      { .label = GetRayReconstructionPresetName(RayReconstructionPreset::K) },
      { .label = GetRayReconstructionPresetName(RayReconstructionPreset::L) },
      { .label = GetRayReconstructionPresetName(RayReconstructionPreset::M), .tooltip = "The SDK header marks it not recommended" },
      { .label = GetRayReconstructionPresetName(RayReconstructionPreset::N), .tooltip = "The SDK header marks it not recommended" },
      { .label = GetRayReconstructionPresetName(RayReconstructionPreset::O), .tooltip = "The SDK header marks it not recommended" },
    };
    static_assert(std::size(RR_PRESETS) == size_t(RayReconstructionPreset::Count));

    constexpr EnumOption RR_SPECULAR_GUIDES[] = {
      { .label = GetRayReconstructionSpecularGuideName(RayReconstructionSpecularGuide::MotionVectors),
        .tooltip = "Computed by the tracer; they cover a moving reflector and a moving reflected object" },
      { .label = GetRayReconstructionSpecularGuideName(RayReconstructionSpecularGuide::HitDistance),
        .tooltip = "From it RR builds its own specular motion out of the camera alone" },
    };
    static_assert(std::size(RR_SPECULAR_GUIDES) == size_t(RayReconstructionSpecularGuide::Count));

    Render* RequireRender(EditorContext& context)
    {
      if (context.render == nullptr)
        PropertyStatus(nullptr, "Render not available");
      return context.render;
    }

    struct TokenHeading
    {
      const char* firstKey;
      const char* heading;
    };

    // Keyed by the first token of each section in VisitEditorThemeTokens order
    constexpr TokenHeading TOKEN_HEADINGS[] = {
      { "app_background", "Surfaces" },
      { "border_subtle", "Borders" },
      { "text_primary", "Text" },
      { "accent", "Accent" },
      { "success", "Semantic" },
      { "axis_x", "Axes" },
      { "radius_small", "Metrics" },
      { "font_size_body", "Typography" },
    };

    const char* FindTokenHeading(std::string_view key)
    {
      for (const TokenHeading& entry : TOKEN_HEADINGS)
      {
        if (key == entry.firstKey)
          return entry.heading;
      }
      return nullptr;
    }

    const char* FindTokenTooltip(std::string_view key)
    {
      if (key == "overlay")
        return "Strips and popups drawn over the viewport image; the alpha is part of the token";
      if (key == "accent_muted")
        return "Selection backgrounds and slider fills";
      if (key == "label_column_fraction")
        return "Share of a property grid's width taken by the label column";
      if (key.starts_with("font_size"))
        return "Ascender to descender height in pixels at 100% scale, about 1.2 times the em size";
      return nullptr;
    }

    // "frame_padding" -> "Frame Padding"
    std::string TokenLabel(std::string_view key)
    {
      std::string label(key);
      bool wordStart = true;
      for (char& c : label)
      {
        if (c == '_')
        {
          c = ' ';
          wordStart = true;
          continue;
        }
        if (wordStart)
          c = char(std::toupper(uint8_t(c)));
        wordStart = false;
      }
      return label;
    }

    PropertyEdit TokenRow(const char* label, const char* key, glm::vec4& color, const glm::vec4&)
    {
      PropertyEdit edit;
      if (!BeginPropertyRow(label, { .tooltip = FindTokenTooltip(key) }))
        return edit;

      // Decoded like every token, so the swatch shows the color the editor actually draws; ImGui's
      // own previews show the raw value and look washed out on the sRGB swapchain
      const float height = ImGui::GetFrameHeight();
      if (ImGui::ColorButton("Swatch", ToImGuiColor(color), ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoTooltip,
        ImVec2(height * 2.0f, height)))
      {
        ImGui::OpenPopup("Picker");
      }
      if (ImGui::BeginPopup("Picker"))
      {
        edit.changed |= ImGui::ColorPicker4("##Picker", &color.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoSidePreview);
        ImGui::EndPopup();
      }

      ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
      EditorFonts::Push(EditorFontRole::Mono);
      // An empty label pushes the row's own id, so the hex field gets an exact bridge path
      // ("<Row>/##Text") instead of a wildcard
      edit.changed |= ImGui::ColorEdit4("", &color.x,
        ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_DisplayHex);
      EditorFonts::Pop();

      EndPropertyRow();
      return edit;
    }

    PropertyEdit TokenRow(const char* label, const char* key, float& value, const float& defaultValue)
    {
      const std::string_view name(key);
      if (name == "label_column_fraction")
      {
        return PropertyFloat(label, value, {
          .min = 0.1f, .max = 0.9f, .speed = 0.005f, .format = "%.2f",
          .defaultValue = defaultValue, .tooltip = FindTokenTooltip(key) });
      }
      if (name.starts_with("font_size"))
      {
        return PropertyFloat(label, value, {
          .min = 8.0f, .max = 48.0f, .speed = 0.1f, .format = "%.1f", .unit = "px",
          .defaultValue = defaultValue, .tooltip = FindTokenTooltip(key) });
      }
      return PropertyFloat(label, value, {
        .min = 0.0f, .max = MAX_METRIC_PIXELS, .speed = 0.1f, .format = "%.1f", .unit = "px",
        .defaultValue = defaultValue, .tooltip = FindTokenTooltip(key) });
    }

    PropertyEdit TokenRow(const char* label, const char* key, glm::vec2& value, const glm::vec2& defaultValue)
    {
      return PropertyVec2(label, value, {
        .speed = 0.1f, .min = 0.0f, .max = MAX_METRIC_PIXELS, .format = "%.1f", .unit = "px",
        .defaultValue = defaultValue, .tooltip = FindTokenTooltip(key) });
    }
  }

  DeveloperPanel::DeveloperPanel(EditorPreferences& preferences)
    : m_Preferences(preferences)
  {
  }

  void DeveloperPanel::OnRender(EditorContext& context)
  {
    if (BeginPanel())
    {
      for (const Group& group : GROUPS)
      {
        if (BeginPropertyGroup(group.label, { .icon = group.icon, .defaultOpen = false, .tooltip = group.tooltip }))
        {
          (this->*group.draw)(context);
          EndPropertyGroup();
        }
      }
    }
    ImGui::End();

    // Outside the panel's window, so they stay up while the panel is a background tab
    DrawImGuiToolWindows();
  }

  void DeveloperPanel::DrawGtaoGroup(EditorContext& context)
  {
    Render* render = RequireRender(context);
    if (render == nullptr)
      return;

    // The GTAO toggle lives in Render Settings, so rows are disabled one by one instead of indented
    // under a parent row this panel does not draw
    const char* gtaoOff = render->GetAOEnabled() ? nullptr : "Requires GTAO (Render Settings > Lighting & GI)";
    const char* replacedBySsgi = gtaoOff != nullptr ? gtaoOff
      : render->GetSSGIEnabled() ? "No effect while SSGI is on: SSGI replaces the diffuse occlusion approximation"
      : nullptr;
    PropertyFloat("AO Strength", render->GetAOStrength(), {
      .min = 0.0f, .max = 1.0f, .format = "%.2f", .slider = true, .defaultValue = 1.0f,
      .tooltip = "Normalized 0-1. Fades the diffuse ambient occlusion. No effect while SSGI is on.",
      .disabledReason = replacedBySsgi });
    PropertyFloat("AO Multi Bounce", render->GetAOMultiBounce(), {
      .min = 0.0f, .max = 1.0f, .format = "%.2f", .slider = true, .defaultValue = 1.0f,
      .tooltip = "Normalized 0-1. Approximated interreflection inside occluded areas. No effect while SSGI is on: SSGI "
                 "computes that bounce for real.",
      .disabledReason = replacedBySsgi });

    PropertySubHeading("Heuristics");
    PropertyFloat("Radius Multiplier", render->GetAORadiusMultiplier(), {
      .min = 0.3f, .max = 3.0f, .speed = 0.01f, .format = "%.3f", .defaultValue = 1.457f,
      .tooltip = "Scales GTAO Radius to make up for the occlusion screen-space sampling misses. Fitted by Intel against a "
                 "ray traced reference; change it only with a reference image to compare against.",
      .disabledReason = gtaoOff });
    PropertyFloat("Falloff Range", render->GetAOFalloffRange(), {
      .min = 0.0f, .max = 1.0f, .speed = 0.01f, .format = "%.3f", .defaultValue = 0.615f,
      .tooltip = "Normalized 0-1. Share of the radius over which occluders fade out toward its edge. Fitted by Intel "
                 "against a ray traced reference.",
      .disabledReason = gtaoOff });
    PropertyFloat("Sample Distribution Power", render->GetAOSampleDistributionPower(), {
      .min = 1.0f, .max = 3.0f, .speed = 0.01f, .format = "%.3f", .defaultValue = 2.0f,
      .tooltip = "Concentrates the steps of each slice near the pixel; higher values sample nearby geometry more densely. "
                 "Fitted by Intel against a ray traced reference.",
      .disabledReason = gtaoOff });
    PropertyFloat("Thin Occluder Compensation", render->GetAOThinOccluderCompensation(), {
      .min = 0.0f, .max = 0.7f, .speed = 0.01f, .format = "%.3f", .defaultValue = 0.0f,
      .tooltip = "Treats occluders as thinner, reducing the occlusion behind thin objects. Fitted by Intel against a ray "
                 "traced reference.",
      .disabledReason = gtaoOff });
    PropertyFloat("Final Value Power", render->GetAOFinalValuePower(), {
      .min = 0.5f, .max = 5.0f, .speed = 0.01f, .format = "%.3f", .defaultValue = 2.2f,
      .tooltip = "Power applied to the final visibility; higher values darken the occlusion. Fitted by Intel against a "
                 "ray traced reference.",
      .disabledReason = gtaoOff });
    PropertyFloat("Depth Mip Sampling Offset", render->GetAODepthMipSamplingOffset(), {
      .min = 0.0f, .max = 30.0f, .speed = 0.01f, .format = "%.3f", .defaultValue = 3.3f,
      .tooltip = "Bias of the depth mip level each step reads; higher values read coarser mips, which is faster and less "
                 "stable. Fitted by Intel against a ray traced reference.",
      .disabledReason = gtaoOff });
  }

  void DeveloperPanel::DrawTaaGroup(EditorContext& context)
  {
    Render* render = RequireRender(context);
    if (render == nullptr)
      return;

    const bool taaResolves = UsesTAAPass(render->GetEffectiveAntialiasingMode()) && !render->IsPathTracingActive();
    PropertyFloat("Clamp Sigma", render->GetTAAClampSigma(), {
      .min = 0.0f, .max = 8.0f, .speed = 0.01f, .format = "%.3f", .defaultValue = 0.979f,
      .tooltip = "Width of the history clipping box in standard deviations of the pixel neighbourhood. Low values collapse "
                 "the box on locally uniform areas and throw away converged history on sub-pixel geometry; high values let "
                 "stale history ghost.",
      .disabledReason = taaResolves ? nullptr
        : "Only used while TAA resolves the frame: Anti-aliasing Mode TAA on the Raster render path" });
  }

  void DeveloperPanel::DrawPathTracerGroup(EditorContext& context)
  {
    Render* render = RequireRender(context);
    if (render == nullptr)
      return;

    const char* unavailable = render->IsPathTracerAvailable() ? nullptr : "The path tracer is unavailable on this device";

    bool devResolve = render->IsPathTraceDevResolveEnabled();
    if (PropertyBool("Developer Resolve", devResolve, {
      .defaultValue = false,
      .tooltip = "No denoiser: resolves the path tracer out of its accumulated mean instead of DLSS Ray Reconstruction - "
                 "one noisy sample while anything moves, converging to the reference while nothing does. Traces 1:1. "
                 "Without Ray Reconstruction it is what makes the Path Tracing render path selectable.",
      .disabledReason = unavailable }))
    {
      render->SetPathTraceDevResolve(devResolve);
    }

    // The render path presents the running mean and the reference view displays it; a diagnostic
    // view stores its diagnostic instead of a sample
    const bool accumulating = !render->IsPathTraceDebugView()
      && (render->IsPathTracingActive() || render->GetDebugView() == DEBUG_VIEW_PT_REFERENCE);

    char samples[32];
    if (accumulating)
      std::snprintf(samples, sizeof(samples), "%d", render->GetPathTraceSampleCount() + 1);
    else
      std::snprintf(samples, sizeof(samples), "Not accumulating");
    PropertyReadOnly("Accumulated Samples", samples, {
      .mono = true,
      .tooltip = "Samples in the running mean. It accumulates only under the Path Tracing render path or the PT "
                 "Reference view." });

    if (PropertyButton("Reset Accumulation", {
      .icon = ICON_LC_REFRESH_CW,
      .tooltip = "Restarts the running mean. It also restarts on its own whenever the camera moves, the scene or the "
                 "lights change, or the render resolution does.",
      .disabledReason = unavailable != nullptr ? unavailable
        : accumulating ? nullptr
        : "Nothing accumulates: requires the Path Tracing render path or the PT Reference view" }))
    {
      render->ResetPathTraceAccumulation();
    }
  }

  void DeveloperPanel::DrawRayReconstructionGroup(EditorContext& context)
  {
    Render* render = RequireRender(context);
    if (render == nullptr)
      return;

    const bool available = render->IsRayReconstructionAvailable();
    const std::string& reason = render->GetRayReconstructionUnavailableReason();
    const std::string unavailable = "DLSS Ray Reconstruction is unavailable: "
      + (reason.empty() ? std::string("not supported on this device") : reason);

    const char* disabledReason = available ? nullptr : unavailable.c_str();
    RayReconstructionSettings& settings = render->GetRayReconstructionSettings();
    PropertyEnum("Preset", settings.preset, RR_PRESETS, {
      .defaultValue = int32_t(RayReconstructionPreset::Default),
      .tooltip = "Network Ray Reconstruction runs. NVIDIA recommends shipping Default; the named presets are for "
                 "experimentation. A change restarts the RR history.",
      .disabledReason = disabledReason });
    PropertyEnum("Specular Guide", settings.specularGuide, RR_SPECULAR_GUIDES, {
      .defaultValue = int32_t(RayReconstructionSpecularGuide::MotionVectors),
      .tooltip = "Which specular guide RR is tagged with - never both. A change restarts the RR history.",
      .disabledReason = disabledReason });

    const bool resolving = render->IsPathTracingActive() && !render->IsPathTraceDevResolveEnabled();
    if (available && !resolving)
      PropertyStatus(nullptr, "Applies while Ray Reconstruction resolves the Path Tracing render path", StatusKind::Info);
  }

  void DeveloperPanel::DrawIrradianceVolumeDiagnosticsGroup(EditorContext& context)
  {
    Render* render = RequireRender(context);
    if (render == nullptr)
      return;

    const Entity entity = context.selectedEntity;
    if (context.scene == nullptr || entity == entt::null || !context.scene->GetRegistry().valid(entity)
      || !context.scene->HasComponent<IrradianceVolumeComponent>(entity))
    {
      PropertyStatus(nullptr, "Select an entity with an Irradiance Volume component");
      return;
    }

    Scene& scene = *context.scene;
    const std::string name = EditorCommands::GetEntityDisplayName(scene.GetRegistry(), entity);
    PropertyReadOnly("Volume", name.c_str(), { .tooltip = "The selected entity" });

    const IrradianceVolumePlacementPreview* preview = render->FindIrradianceVolumePlacementPreview(entity);
    if (preview == nullptr)
    {
      PropertyStatus(nullptr, "No placement preview yet: Preview Placement in the Details panel lays one out", StatusKind::Info);
      return;
    }

    const bool stale = Render::ComputeIrradianceVolumePlacementFingerprint(scene, entity) != preview->fingerprint;
    PropertyStatus("Preview", stale ? "Stale: the volume changed since the preview" : "Current",
      stale ? StatusKind::Warning : StatusKind::Success,
      "Only this volume's own parameters are tracked: its transform, half extents, spacings and backface threshold. Scene "
      "geometry, bake overrides, hidden entities and model reloads are not - preview again after changing those.");

    const auto drawTimings = [preview]()
    {
      PropertySubHeading("Timings");
      char text[48];
      std::snprintf(text, sizeof(text), "%.2f s", preview->totalSeconds);
      PropertyReadOnly("Total Time", text, { .mono = true, .tooltip = "The whole preview, bake scene setup included" });
      std::snprintf(text, sizeof(text), "%.2f s", preview->layoutSeconds);
      PropertyReadOnly("Layout Time", text, { .mono = true, .tooltip = "The brick layout, its geometry queries included" });
      std::snprintf(text, sizeof(text), "%.2f s", preview->querySeconds);
      PropertyReadOnly("Query Time", text, { .mono = true, .tooltip = "Ray traced geometry queries on the GPU" });
      std::snprintf(text, sizeof(text), "%u", preview->queryPoints);
      PropertyReadOnly("Query Points", text, { .mono = true, .tooltip = "Points the layout asked the GPU about" });
      std::snprintf(text, sizeof(text), "%u", preview->queryBatches);
      PropertyReadOnly("Query Batches", text, { .mono = true, .tooltip = "GPU submissions the queries were split into" });
    };

    if (!preview->error.empty())
    {
      PropertyStatus("Layout", preview->error.c_str(), StatusKind::Error, "Why the brick layout could not be built");
      drawTimings();
      return;
    }

    PropertyStatus("Validation", preview->validationPassed ? "Passed" : preview->validationFailure.c_str(),
      preview->validationPassed ? StatusKind::Success : StatusKind::Error,
      "Consistency checks the layout ran through before it was kept");

    const IrradianceVolumePlacementEstimate layout = Render::EstimateIrradianceVolumePlacement(*preview, 0);

    PropertySubHeading("Levels");
    if (ImGui::BeginTable("Placement Levels", 4, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchSame))
    {
      ImGui::TableSetupColumn("Spacing");
      ImGui::TableSetupColumn("Bricks");
      ImGui::TableSetupColumn("Nodes");
      ImGui::TableSetupColumn("Stitched");
      ImGui::TableHeadersRow();

      EditorFonts::Push(EditorFontRole::Mono);
      for (uint32_t level = preview->maxSpacingIndex + 1; level-- > preview->minSpacingIndex;)
      {
        const IrradianceBrickLevelStats& stats = preview->levelStats[level];
        // The brick gizmo's color for this level, so rows and viewport bricks can be matched up
        const glm::vec4 color = Render::GetPlacementBrickColor(level);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(color.r, color.g, color.b, 1.0f), "%g m", double(IRRADIANCE_SPACINGS[level]));
        ImGui::TableNextColumn();
        ImGui::Text("%u", stats.bricks);
        ImGui::TableNextColumn();
        ImGui::Text("%u", stats.uniqueNodes);
        ImGui::TableNextColumn();
        ImGui::Text("%u", stats.stitchedNodes);
      }

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Total");
      ImGui::TableNextColumn();
      ImGui::Text("%u", layout.bricks);
      ImGui::TableNextColumn();
      ImGui::Text("%u", layout.uniqueNodes);
      ImGui::TableNextColumn();
      ImGui::Text("%u", layout.stitchedNodes);
      EditorFonts::Pop();
      ImGui::EndTable();
    }

    PropertySubHeading("Estimates");
    constexpr double BYTES_PER_MB = 1024.0 * 1024.0;
    char text[96];
    std::snprintf(text, sizeof(text), "~%.1f MB", double(layout.runtimeBytes) / BYTES_PER_MB);
    PropertyReadOnly("VRAM", text, { .mono = true,
      .tooltip = "Estimated for the brick format: 24 bytes per brick texel (three RGBA16F coefficient textures) plus 4 "
                 "per indirection cell." });
    std::snprintf(text, sizeof(text), "~%.1f MB", double(layout.diskBytes) / BYTES_PER_MB);
    PropertyReadOnly("Disk", text, { .mono = true,
      .tooltip = "25 bytes per unique node, the node indices of every brick and 4 per indirection cell." });
    std::snprintf(text, sizeof(text), "%u", layout.indirectionCells);
    PropertyReadOnly("Indirection Cells", text, { .mono = true, .tooltip = "Cells of the grid that maps positions to bricks" });

    const uint32_t volumeSamples = uint32_t(std::clamp(render->GetVolumeSampleCount(),
      Render::MIN_VOLUME_SAMPLES, Render::MAX_VOLUME_SAMPLES));
    const IrradianceVolumePlacementEstimate bake = Render::EstimateIrradianceVolumePlacement(*preview, volumeSamples);
    std::snprintf(text, sizeof(text), "%u x %u = %llu", bake.bakedNodes, volumeSamples, (unsigned long long)bake.primarySamples);
    PropertyReadOnly("Bake Samples", text, { .mono = true,
      .tooltip = "Baked nodes times Volume Samples (Render Settings > Baking). Unique nodes that are not stitched get "
                 "integrated, each sample tracing a path of up to Volume Bounces bounces; stitched nodes are interpolated "
                 "from their coarser neighbour instead." });
    if (bake.primarySamples > BakeLimits::VOLUME_WARN_PRIMARY_SAMPLES)
      PropertyStatus(nullptr, "Many primary samples: the bake will take a while", StatusKind::Warning);

    // Bricks are toggled from the viewport toolbar's Show menu; with them or the gizmos off no brick is
    // drawn, so a drawn share would be wrong
    if (render->GetVolumeBricksVisible() && render->GetGizmosEnabled())
    {
      const auto strides = Render::GetPreviewBrickDrawStrides(*preview, render->GetVolumeNodeGizmosDrawn());
      uint32_t drawnBricks = 0;
      for (uint32_t level = 0; level < uint32_t(strides.size()); level++)
        drawnBricks += (preview->levelStats[level].bricks + strides[level] - 1) / strides[level];

      std::snprintf(text, sizeof(text), "%u of %u", drawnBricks, layout.bricks);
      char tooltip[256];
      std::snprintf(tooltip, sizeof(tooltip), "The brick gizmo draws every k-th brick of each spacing level, so every level "
        "stays visible: at most %u bricks, fewer by the nodes Volume Nodes draws, down to %u.",
        Render::MAX_DRAWN_PREVIEW_BRICKS, Render::MAX_DRAWN_PREVIEW_BRICKS_WITH_NODES);
      PropertyReadOnly("Drawn Bricks", text, { .mono = true, .tooltip = tooltip });
    }

    drawTimings();
  }

  void DeveloperPanel::DrawReflectionProbeDiagnosticsGroup(EditorContext& context)
  {
    Render* render = RequireRender(context);
    if (render == nullptr)
      return;

    const Entity entity = context.selectedEntity;
    if (context.scene == nullptr || entity == entt::null || !context.scene->GetRegistry().valid(entity)
      || !context.scene->HasComponent<ReflectionProbeComponent>(entity))
    {
      PropertyStatus(nullptr, "Select an entity with a Reflection Probe component");
      return;
    }

    Scene& scene = *context.scene;
    const ReflectionProbeComponent& probe = scene.GetComponent<ReflectionProbeComponent>(entity);
    const std::string name = EditorCommands::GetEntityDisplayName(scene.GetRegistry(), entity);
    PropertyReadOnly("Probe", name.c_str(), { .tooltip = "The selected entity" });

    if (!probe.baked)
    {
      PropertyStatus(nullptr, "Not baked: Bake Probe in the Details panel captures it", StatusKind::Warning);
      return;
    }

    char slot[16];
    std::snprintf(slot, sizeof(slot), "%u", probe.atlasSlot);
    PropertyReadOnly("Atlas Slot", slot, { .mono = true, .tooltip = "Index of the probe's cubemap in the reflection probe atlas" });
    if (!probe.bakedPrefilterPath.empty() && context.assetManager != nullptr)
    {
      const std::string relativePath = context.assetManager->MakeRelative(probe.bakedPrefilterPath);
      PropertyReadOnly("Prefilter File", relativePath.c_str(), { .mono = true, .tooltip = probe.bakedPrefilterPath.c_str() });
    }

    PropertySubHeading("Prefilter Faces");
    constexpr const char* FACE_LABELS[] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
    constexpr float MAX_FACE_SIZE = 128.0f;
    constexpr uint32_t FACES_PER_ROW = 3;

    const ImGuiStyle& style = ImGui::GetStyle();
    const float faceSize = std::max(std::floor(std::min(
      (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x * float(FACES_PER_ROW - 1)) / float(FACES_PER_ROW),
      MAX_FACE_SIZE * EditorStyle::GetContentScale())), 1.0f);

    auto& atlas = render->GetProbeAtlas();
    auto& renderContext = render->GetContext();
    for (uint32_t face = 0; face < uint32_t(std::size(FACE_LABELS)); face++)
    {
      if (face % FACES_PER_ROW != 0)
        ImGui::SameLine();
      ImGui::BeginGroup();
      VkDescriptorSet descriptor = atlas.GetPrefilterFacePreview(renderContext, probe.atlasSlot, face);
      ImGui::Image((ImTextureID)descriptor, ImVec2(faceSize, faceSize));
      ImGui::TextDisabled("%s", FACE_LABELS[face]);
      ImGui::EndGroup();
    }
  }

  void DeveloperPanel::DrawThemeGroup(EditorContext&)
  {
    EditorTheme theme = EditorStyle::GetRequestedTheme();
    const EditorTheme defaults = MakeEditorTheme(m_Preferences.themePreset);
    bool changed = false;

    if (PropertyButton("Save", {
      .icon = ICON_LC_SAVE,
      .tooltip = "Writes the theme to editor.yaml, so the next runs start with it",
      .disabledReason = theme == m_Preferences.theme ? "The theme has no unsaved changes" : nullptr }))
    {
      m_Preferences.theme = theme;
      m_Preferences.Save();
    }

    if (PropertyButton("Reset to Defaults", {
      .icon = ICON_LC_ROTATE_CCW,
      .tooltip = "Puts every token back to the value of the theme picked in View > Theme; Save keeps the result",
      .disabledReason = theme == defaults ? "Every token already has the picked theme's value" : nullptr }))
    {
      theme = defaults;
      changed = true;
    }

    if (theme != m_Preferences.theme)
      PropertyStatus(nullptr, "Unsaved changes last until the editor closes", StatusKind::Warning);

    if (m_TokenLabels.empty())
      VisitEditorThemeTokens([this](const char* key, auto) { m_TokenLabels.push_back(TokenLabel(key)); });

    size_t tokenIndex = 0;
    VisitEditorThemeTokens([&](const char* key, auto member)
    {
      if (const char* heading = FindTokenHeading(key))
        PropertySubHeading(heading);

      changed |= TokenRow(m_TokenLabels[tokenIndex++].c_str(), key, theme.*member, defaults.*member).changed;
    });

    if (changed)
    {
      ClampEditorTheme(theme);
      EditorStyle::RequestTheme(theme);
    }
  }

  void DeveloperPanel::DrawImGuiToolsGroup(EditorContext&)
  {
    PropertyBool("Metrics Window", b_ShowMetrics, {
      .tooltip = "Dear ImGui's Metrics/Debugger: windows, draw lists, item ids, docking and fonts" });
    PropertyBool("Style Editor", b_ShowStyleEditor, {
      .tooltip = "Dear ImGui's raw style editor. Its edits are not saved, and the next theme change replaces them" });
  }

  void DeveloperPanel::DrawImGuiToolWindows()
  {
    if (b_ShowMetrics)
      ImGui::ShowMetricsWindow(&b_ShowMetrics);

    if (b_ShowStyleEditor)
    {
      if (ImGui::Begin("Dear ImGui Style Editor", &b_ShowStyleEditor))
        ImGui::ShowStyleEditor();
      ImGui::End();
    }
  }
}
