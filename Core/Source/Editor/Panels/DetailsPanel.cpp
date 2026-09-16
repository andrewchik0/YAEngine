#include "Editor/Panels/DetailsPanel.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include "Editor/EditorCommands.h"
#include "Editor/EditorContext.h"
#include "Editor/Panels/MaterialInspectorPanel.h"
#include "Editor/Panels/SequencerPanel.h"
#include "Editor/Utils/CurveEditor.h"
#include "Editor/Utils/EditorIcons.h"
#include "Editor/Utils/EditorWidgets.h"
#include "Editor/Utils/FileDialog.h"
#include "Editor/Utils/SplinePathEditor.h"
#include "Scene/BakeExclusion.h"
#include "Scene/Components.h"
#include "Scene/ModelOverrides.h"
#include "Scene/Scene.h"
#include "Assets/AssetManager.h"
#include "Render/BakeLimits.h"
#include "Render/Render.h"
#include "Utils/IrradianceGrid.h"
#include "Utils/StringSearch.h"

namespace YAEngine
{
  using namespace EditorWidgets;

  namespace
  {
    constexpr double BYTES_PER_MB = 1024.0 * 1024.0;
    constexpr size_t MATERIAL_PICKER_VISIBLE_ROWS = 12;
    constexpr size_t MAX_RESOLUTION_OPTIONS = 8;
    constexpr float ROTATION_EPSILON = 1e-6f;

    constexpr const char* PREVIEW_UNAVAILABLE = "The placement preview finds geometry by ray tracing, and the ray traced "
      "baker is unavailable: no hardware ray tracing pipeline or no bindless texture table.";
    constexpr const char* VOLUME_BAKE_UNAVAILABLE = "Irradiance volumes bake by ray tracing, and the ray traced baker is "
      "unavailable: no hardware ray tracing pipeline or no bindless texture table.";

    constexpr EnumOption LIGHT_TYPES[] = {
      { .label = "Point", .tooltip = "Shines in every direction from the entity, out to Radius" },
      { .label = "Spot", .tooltip = "Shines a cone along the entity's rotation, out to Radius" },
      { .label = "Directional", .tooltip = "Parallel light along the entity's rotation, like the sun; its position does not matter" },
    };

    constexpr EnumOption PROBE_SHAPES[] = {
      { .label = "Sphere", .tooltip = "Influence sphere of Radius around the entity" },
      { .label = "Box", .tooltip = "Influence box of Extents, rotated with the entity" },
    };

    constexpr EnumOption SPACING_OPTIONS[] = {
      { .label = "0.25 m" }, { .label = "0.5 m" }, { .label = "1 m" }, { .label = "2 m" }, { .label = "4 m" },
    };
    static_assert(std::size(SPACING_OPTIONS) == IRRADIANCE_SPACINGS.size(), "Spacing options must match IRRADIANCE_SPACINGS");

    constexpr EnumOption BAKE_MODES[] = {
      { .label = "Auto", .tooltip = "Captured unless it or an ancestor has a dynamic collider" },
      { .label = "Include", .tooltip = "Always captured, together with everything below it" },
      { .label = "Exclude", .tooltip = "Never captured, together with everything below it" },
    };

    constexpr EnumOption NOISE_TYPES[] = {
      { .label = "fBm", .tooltip = "Rolling hills: octaves of plain noise added together" },
      { .label = "Ridged", .tooltip = "Sharp crests and creased valleys, like eroded mountains" },
      { .label = "Billowy", .tooltip = "Rounded bumps with sharp creases between them, like dunes" },
    };

    // In ScatterMeshType order
    constexpr EnumOption SCATTER_MESH_TYPES[] = {
      { .label = "Model", .tooltip = "Instances of the scatter's model file" },
      { .label = "Plane", .tooltip = "Two crossed, double-sided quads with a texture, for grass and flowers" },
    };

    constexpr nfdu8filteritem_t HEIGHTMAP_FILTERS[] = { { "Image", "png,jpg,tga,bmp" } };
    constexpr nfdu8filteritem_t SCATTER_TEXTURE_FILTERS[] = { { "Image", "png,jpg,jpeg,tga,bmp" } };

    constexpr uint32_t DEFAULT_COLLIDER_LAYER = 1u;
    constexpr uint32_t DEFAULT_COLLIDER_MASK = ~0u;

    // Share of the road canvas left free around the points, so one can be dragged a little past them
    constexpr float ROAD_VIEW_MARGIN = 0.1f;
    constexpr float MIN_ROAD_VIEW_RANGE = 1e-4f;
    constexpr float DEFAULT_ROAD_VIEW_RANGE = 10.0f;

    constexpr const char* SATELLITE_UNUSED = "Unused with a Cluster Source: the instances are placed around the "
      "source's instances instead.";

    const std::string ROAD_PATH_TOOLTIP = std::string("Top-down view of the control points, +X to the right and +Z "
      "down. The view refits to the points once an edit ends. ") + SplinePathEditor::GESTURES;
    const std::string MASK_PATH_TOOLTIP = std::string("Path drawn top-down over the terrain, 0-1 across its width and "
      "depth. The height mask is 0 along it and rises to full height at Falloff Radius. ") + SplinePathEditor::GESTURES;
    const std::string FALLOFF_CURVE_TOOLTIP = std::string("X: distance from the mask path, 0 on it and 1 at Falloff "
      "Radius. Y: the height multiplier at that distance. ") + CurveEditor::GESTURES;
    const std::string CARVE_CURVE_TOOLTIP = std::string("X: carve strength from the radii, 0 at Carve Outer Radius and "
      "1 within Carve Inner Radius. Y: the strength used instead. ") + CurveEditor::GESTURES;

    bool SameRotation(const glm::quat& a, const glm::quat& b)
    {
      return std::abs(a.x - b.x) <= ROTATION_EPSILON && std::abs(a.y - b.y) <= ROTATION_EPSILON
        && std::abs(a.z - b.z) <= ROTATION_EPSILON && std::abs(a.w - b.w) <= ROTATION_EPSILON;
    }

    int32_t FindSpacingIndex(float spacing)
    {
      for (size_t i = 0; i < IRRADIANCE_SPACINGS.size(); i++)
      {
        if (IRRADIANCE_SPACINGS[i] == spacing)
          return int32_t(i);
      }
      return 0;
    }

    // Everything a bake renders is a mesh, plus the roots whose override covers a subtree of meshes.
    // An override that already exists stays visible wherever it was set.
    bool ShowsBakeInclusion(Scene& scene, Entity entity)
    {
      return scene.HasComponent<MeshComponent>(entity) || scene.HasComponent<ModelSourceComponent>(entity)
        || scene.HasComponent<TerrainComponent>(entity) || scene.HasComponent<ScatterComponent>(entity)
        || scene.HasComponent<BakeOverrideComponent>(entity);
    }

    // One header for every component section. A removable section gets the Remove button; the caller
    // removes the component after the section is drawn, so no row sees it vanish mid-frame.
    bool BeginSection(const char* label, const char* icon, const char* tooltip,
      bool* removeRequested = nullptr, const char* removeDisabledReason = nullptr)
    {
      char removeTooltip[96];
      std::snprintf(removeTooltip, sizeof(removeTooltip), "Removes the %s component from the entity", label);

      GroupSpec spec { .icon = icon, .defaultOpen = true, .tooltip = tooltip };
      if (removeRequested != nullptr)
      {
        spec.action = GroupAction {
          .label = "Remove",
          .icon = ICON_LC_TRASH_2,
          .tooltip = removeTooltip,
          .disabledReason = removeDisabledReason,
          .pressed = removeRequested,
        };
      }
      return BeginPropertyGroup(label, spec);
    }

    std::string DescribeBakeInclusion(Scene& scene, Entity entity)
    {
      entt::registry& registry = scene.GetRegistry();
      if (registry.all_of<HiddenTag>(entity))
        return "hidden - its meshes are not rendered into bakes, its lights still contribute";

      const BakeInclusion inclusion = ResolveBakeInclusion(registry, entity);
      std::string text = inclusion.excluded ? "excluded" : "included";
      if (inclusion.reason == BakeInclusionReason::Default)
        return text;

      const bool byOverride = inclusion.reason != BakeInclusionReason::DynamicCollider;
      if (inclusion.source == entity)
        return text + (byOverride ? " by this override" : " by its dynamic collider");

      return text + (byOverride ? " by the override on '" : " by the dynamic collider on '")
        + EditorCommands::GetEntityDisplayName(registry, inclusion.source) + "'";
    }

    // Search field and clipped, name-sorted list for a material picker popup. Returns the material
    // clicked this frame, or an invalid handle.
    MaterialHandle DrawMaterialList(MaterialManager& materials, MaterialHandle current, char* search, size_t searchSize)
    {
      const bool appearing = ImGui::IsWindowAppearing();
      if (appearing)
      {
        search[0] = '\0';
        ImGui::SetKeyboardFocusHere();
      }
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputTextWithHint("##Search", ICON_LC_SEARCH " Search", search, searchSize);

      struct Entry
      {
        MaterialHandle handle;
        const std::string* name = nullptr;
      };

      std::vector<Entry> entries;
      materials.ForEachWithHandle([&](MaterialHandle handle, Material& material)
      {
        if (ContainsCaseInsensitive(material.name, search))
          entries.push_back(Entry { .handle = handle, .name = &material.name });
      });
      std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return *a.name < *b.name; });

      MaterialHandle picked = MaterialHandle::Invalid();
      if (entries.empty())
      {
        ImGui::TextDisabled("No matching materials");
        return picked;
      }

      int32_t currentIndex = -1;
      for (size_t i = 0; i < entries.size(); i++)
      {
        if (entries[i].handle == current)
          currentIndex = int32_t(i);
      }

      const float listHeight = ImGui::GetTextLineHeightWithSpacing()
        * float(std::min(entries.size(), MATERIAL_PICKER_VISIBLE_ROWS));
      if (ImGui::BeginChild("##Materials", ImVec2(0.0f, listHeight)))
      {
        ImGuiListClipper clipper;
        clipper.Begin(int32_t(entries.size()));
        if (appearing && currentIndex >= 0)
          clipper.IncludeItemByIndex(currentIndex);

        while (clipper.Step())
        {
          for (int32_t i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
          {
            const Entry& entry = entries[size_t(i)];
            const bool selected = i == currentIndex;
            ImGui::PushID(int32_t(entry.handle.index));
            if (ImGui::Selectable(entry.name->empty() ? "(unnamed)" : entry.name->c_str(), selected) && !selected)
            {
              picked = entry.handle;
              ImGui::CloseCurrentPopup();
            }
            if (selected && appearing)
              ImGui::SetScrollHereY();
            ImGui::PopID();
          }
        }
      }
      ImGui::EndChild();
      return picked;
    }

    // One line under Preview Placement; the full report is the Developer panel's diagnostics group
    void DrawPlacementSummary(Scene& scene, Render& render, Entity entity)
    {
      const IrradianceVolumePlacementPreview* preview = render.FindIrradianceVolumePlacementPreview(entity);
      if (preview == nullptr)
        return;

      if (!preview->error.empty())
      {
        PropertyStatus(nullptr, "Placement preview failed", StatusKind::Error, preview->error.c_str());
        return;
      }

      const bool stale = Render::ComputeIrradianceVolumePlacementFingerprint(scene, entity) != preview->fingerprint;
      const IrradianceVolumePlacementEstimate estimate = Render::EstimateIrradianceVolumePlacement(*preview, 0);

      char summary[128];
      std::snprintf(summary, sizeof(summary), "%u bricks, %u nodes, ~%.1f MB VRAM%s", estimate.bricks,
        estimate.uniqueNodes, double(estimate.runtimeBytes) / BYTES_PER_MB, stale ? " (stale)" : "");

      const StatusKind kind = !preview->validationPassed ? StatusKind::Error
        : stale ? StatusKind::Warning
        : StatusKind::Neutral;
      const char* tooltip = !preview->validationPassed
        ? "The layout failed validation. Developer > Irradiance Volume Diagnostics shows why."
        : stale
          ? "The volume changed since the preview: its transform, half extents, spacings or backface threshold. "
            "Preview again. Developer > Irradiance Volume Diagnostics has the full report."
          : "Only this volume's own parameters mark a preview stale. Scene geometry, bake overrides, hidden entities and "
            "model reloads do not - preview again after changing those. Per-level counts, disk estimate, validation and "
            "timings are in Developer > Irradiance Volume Diagnostics.";
      PropertyStatus(nullptr, summary, kind, tooltip);
    }

    // The bake lays its bricks out exactly as the placement preview does, so the kept preview is what
    // predicts its cost.
    void DrawBakeEstimate(Scene& scene, Render& render, Entity entity)
    {
      const IrradianceVolumePlacementPreview* preview = render.FindIrradianceVolumePlacementPreview(entity);
      if (preview == nullptr || !preview->error.empty())
      {
        PropertyStatus(nullptr, "Estimate: Preview Placement shows the node count", StatusKind::Neutral,
          "The bake lays its bricks out exactly as the placement preview does, so the preview predicts its cost.");
        return;
      }

      const uint32_t volumeSamples = uint32_t(std::clamp(render.GetVolumeSampleCount(),
        Render::MIN_VOLUME_SAMPLES, Render::MAX_VOLUME_SAMPLES));
      const IrradianceVolumePlacementEstimate estimate = Render::EstimateIrradianceVolumePlacement(*preview, volumeSamples);
      const bool stale = Render::ComputeIrradianceVolumePlacementFingerprint(scene, entity) != preview->fingerprint;
      const bool manySamples = estimate.primarySamples > BakeLimits::VOLUME_WARN_PRIMARY_SAMPLES;

      char text[160];
      std::snprintf(text, sizeof(text), "Estimate: %u nodes x %u samples = %llu primary samples%s", estimate.bakedNodes,
        volumeSamples, (unsigned long long)estimate.primarySamples, stale ? " (stale preview)" : "");

      char tooltip[640];
      std::snprintf(tooltip, sizeof(tooltip), "%sUnique nodes that are not stitched, times Volume Samples (Render Settings > "
        "Baking), each tracing paths of up to Volume Bounces bounces. Buried nodes are traced like the rest and only rejected "
        "afterwards, those near a back face once more from their virtual offset; stitched nodes are interpolated from their "
        "coarser neighbour instead of baked.", manySamples ? "Many primary samples: the bake will take a while. " : "");
      PropertyStatus(nullptr, text, manySamples ? StatusKind::Warning : StatusKind::Neutral, tooltip);
    }

    void SetCameraToEditorView(Scene& scene, Entity entity)
    {
      Entity editorCamera = entt::null;
      for (auto e : scene.GetView<EditorOnlyTag, CameraComponent>())
      {
        editorCamera = e;
        break;
      }
      if (editorCamera == entt::null)
        return;

      // The editor camera is a root entity, so its LocalTransform is already world space
      const LocalTransform& view = scene.GetTransform(editorCamera);

      Entity parent = scene.HasComponent<HierarchyComponent>(entity)
        ? scene.GetHierarchy(entity).parent
        : Entity(entt::null);

      auto& transform = scene.GetTransform(entity);
      if (parent != entt::null && scene.HasComponent<WorldTransform>(parent))
      {
        const glm::mat4& parentWorld = scene.GetComponent<WorldTransform>(parent).world;
        glm::mat3 basis(parentWorld);
        for (int i = 0; i < 3; i++)
        {
          float length = glm::length(basis[i]);
          if (length > 1e-6f)
            basis[i] /= length;
        }
        glm::quat parentRotation = glm::normalize(glm::quat_cast(basis));
        transform.position = glm::vec3(glm::inverse(parentWorld) * glm::vec4(view.position, 1.0f));
        transform.rotation = glm::normalize(glm::inverse(parentRotation) * view.rotation);
      }
      else
      {
        transform.position = view.position;
        transform.rotation = view.rotation;
      }
      scene.MarkDirty(entity);
    }

    // Terrain and road own the mesh they generate
    void DestroyGeneratedMesh(EditorContext& context, Entity entity)
    {
      Scene& scene = *context.scene;
      if (scene.HasComponent<MeshComponent>(entity))
      {
        auto meshHandle = scene.GetComponent<MeshComponent>(entity).asset;
        if (context.assetManager->Meshes().Has(meshHandle))
        {
          context.render->WaitIdle();
          context.assetManager->Meshes().Destroy(meshHandle);
        }
        scene.RemoveComponent<MeshComponent>(entity);
      }
      if (scene.HasComponent<LocalBounds>(entity))
        scene.RemoveComponent<LocalBounds>(entity);
      if (scene.HasComponent<WorldBounds>(entity))
        scene.RemoveComponent<WorldBounds>(entity);
    }

    struct ColliderFields
    {
      // Model and Scatter build their colliders from mesh bounds and can switch that off; null for the
      // Collider component itself
      bool* enabled = nullptr;
      bool enabledDefault = false;
      glm::vec3* offset = nullptr;
      // Meters for the Collider component, a multiplier on the mesh bounds for a built collider
      glm::vec3* halfExtents = nullptr;
      bool* isStatic = nullptr;
      uint32_t* layer = nullptr;
      uint32_t* mask = nullptr;
    };

    // The one set of collider rows for the Collider component and the colliders Model and Scatter build.
    // Callers that rebuild a collider do so on committed.
    PropertyEdit DrawColliderProperties(const ColliderFields& fields, const char* disabledReason)
    {
      const bool built = fields.enabled != nullptr;
      PropertyEdit edit;
      if (built)
      {
        edit |= PropertyBool("Enabled", *fields.enabled, {
          .defaultValue = fields.enabledDefault,
          .tooltip = "Builds a box collider from the bounds of the meshes; it is rebuilt whenever a value below changes.",
          .disabledReason = disabledReason });
      }

      PushDependency(!built || *fields.enabled, "Requires Enabled");
      edit |= PropertyVec3("Offset", *fields.offset, {
        .speed = 0.05f, .format = "%.2f", .unit = "m", .defaultValue = glm::vec3(0.0f),
        .tooltip = built ? "Moves the box away from the center of the mesh bounds."
                         : "Center of the box relative to the entity origin, in its local space.",
        .disabledReason = disabledReason });
      if (built)
      {
        edit |= PropertyVec3("Half Extents Scale", *fields.halfExtents, {
          .speed = 0.01f, .min = 0.0f, .max = 10.0f, .format = "%.2f", .defaultValue = glm::vec3(1.0f),
          .tooltip = "Multiplies the half size of the mesh bounds along each axis; below 1 pulls the box inside the mesh.",
          .disabledReason = disabledReason });
      }
      else
      {
        edit |= PropertyVec3("Half Extents", *fields.halfExtents, {
          .speed = 0.05f, .min = 0.0f, .max = 1000.0f, .format = "%.2f", .unit = "m", .defaultValue = glm::vec3(0.5f),
          .tooltip = "Half size of the box along each local axis.",
          .disabledReason = disabledReason });
      }
      edit |= PropertyBool("Static", *fields.isStatic, {
        .defaultValue = true,
        .tooltip = "Clear it for colliders that move. Bake Inclusion's Auto mode leaves an entity with a dynamic collider, "
                   "and everything below it, out of bakes.",
        .disabledReason = disabledReason });
      edit |= PropertyBitmask("Layer", *fields.layer, {
        .defaultValue = DEFAULT_COLLIDER_LAYER,
        .tooltip = "Layers the collider is on. A collision query finds it only when the query's mask shares a bit with them.",
        .disabledReason = disabledReason });
      edit |= PropertyBitmask("Mask", *fields.mask, {
        .defaultValue = DEFAULT_COLLIDER_MASK,
        .tooltip = "Layers the collider interacts with: a query made for it finds only colliders whose Layer shares a bit "
                   "with this mask.",
        .disabledReason = disabledReason });
      PopDependency();
      return edit;
    }

    // A point inserted on the canvas takes the mean height of the two points it lands between, so no
    // existing point's height changes
    float InsertedPointHeight(const std::vector<glm::vec3>& points, size_t index)
    {
      if (points.empty())
        return 0.0f;
      if (index == 0)
        return points.front().y;
      if (index >= points.size())
        return points.back().y;
      return (points[index - 1].y + points[index].y) * 0.5f;
    }

    void DrawTerrainContents(EditorContext& context, Entity entity, TerrainComponent& terrain)
    {
      Scene& scene = *context.scene;
      PropertyEdit edit;

      edit |= PropertyFloat("Size", terrain.size, {
        .min = 1.0f, .max = 10000.0f, .speed = 1.0f, .format = "%.1f", .unit = "m", .defaultValue = 100.0f,
        .tooltip = "Width and depth of the square terrain, centered on the entity." });
      edit |= PropertyUInt("Subdivisions", terrain.subdivisions, {
        .min = 2, .max = 512, .speed = 1.0f, .defaultValue = 128,
        .tooltip = "Grid quads along each side; the mesh has (Subdivisions + 1) squared vertices." });
      edit |= PropertyFloat("UV Scale", terrain.uvScale, {
        .min = 0.01f, .max = 100.0f, .speed = 0.1f, .format = "%.2f", .defaultValue = 10.0f,
        .tooltip = "How many times the material's textures repeat across the whole terrain." });
      edit |= PropertyFloat("Height Scale", terrain.heightScale, {
        .min = 0.0f, .max = 1000.0f, .speed = 0.1f, .format = "%.2f", .unit = "m", .defaultValue = 10.0f,
        .tooltip = "Multiplies the heightmap's 0-1 brightness, or the noise: -1 to 1 without a height mask, 0 to 1 "
                   "with one." });

      PropertySubHeading("Heightmap");
      const bool hasHeightmap = !terrain.heightmapPath.empty();
      const std::string heightmap = hasHeightmap ? context.assetManager->MakeRelative(terrain.heightmapPath) : std::string("None");
      PropertyReadOnly("Image", heightmap.c_str(), {
        .mono = true,
        .tooltip = hasHeightmap ? terrain.heightmapPath.c_str() : "No heightmap: the height comes from the procedural noise." });
      if (PropertyButton("Load Heightmap...", {
        .icon = ICON_LC_FOLDER_OPEN,
        .tooltip = "Picks an image whose brightness times Height Scale becomes the terrain height. Procedural noise and "
                   "the height mask are unused while a heightmap is loaded." }))
      {
        const std::string path = FileDialog::OpenFile(HEIGHTMAP_FILTERS, 1);
        if (!path.empty())
        {
          terrain.heightmapPath = path;
          edit.committed = true;
        }
      }
      if (PropertyButton("Clear Heightmap", {
        .icon = ICON_LC_X, .tooltip = "Goes back to procedural noise",
        .disabledReason = hasHeightmap ? nullptr : "No heightmap loaded" }))
      {
        terrain.heightmapPath.clear();
        edit.committed = true;
      }

      PushDependency(!hasHeightmap, "Unused while a heightmap is loaded");

      PropertySubHeading("Procedural Noise");
      edit |= PropertyEnum("Noise Type", terrain.noiseType, NOISE_TYPES, {
        .defaultValue = int32_t(TerrainNoiseType::FBm), .tooltip = "Character of the noise the height is built from." });
      edit |= PropertyFloat("Frequency", terrain.frequency, {
        .min = 0.001f, .max = 1.0f, .speed = 0.0005f, .format = "%.4f", .defaultValue = 0.02f,
        .tooltip = "Noise cycles per meter in the first octave: lower values give broader hills." });
      edit |= PropertyUInt("Octaves", terrain.octaves, {
        .min = 1, .max = 8, .speed = 0.05f, .defaultValue = 4,
        .tooltip = "Layers of noise added together, each one finer than the last." });
      edit |= PropertyFloat("Lacunarity", terrain.lacunarity, {
        .min = 1.0f, .max = 4.0f, .speed = 0.01f, .format = "%.2f", .defaultValue = 2.0f,
        .tooltip = "Frequency multiplier from one octave to the next." });
      edit |= PropertyFloat("Persistence", terrain.persistence, {
        .min = 0.1f, .max = 1.0f, .speed = 0.01f, .format = "%.2f", .defaultValue = 0.5f,
        .tooltip = "Amplitude multiplier from one octave to the next: lower values keep the fine octaves faint." });
      edit |= PropertyInt("Seed", terrain.seed, {
        .speed = 0.25f, .defaultValue = 0,
        .tooltip = "Shifts the noise: each seed gives a different terrain from the same settings." });

      PropertySubHeading("Domain Warping");
      edit |= PropertyFloat("Warp Strength", terrain.warpStrength, {
        .min = 0.0f, .max = 200.0f, .speed = 0.5f, .format = "%.1f", .unit = "m", .defaultValue = 0.0f,
        .tooltip = "How far a second noise pushes each sample point before the height is read, bending the features. "
                   "0 switches warping off." });
      PushDependency(terrain.warpStrength > 0.0f, "Requires Warp Strength above 0");
      edit |= PropertyFloat("Warp Frequency", terrain.warpFrequency, {
        .min = 0.001f, .max = 0.1f, .speed = 0.0005f, .format = "%.4f", .defaultValue = 0.008f,
        .tooltip = "Cycles per meter of the warping noise." });
      PopDependency();

      PropertySubHeading("Height Mask");
      if (terrain.maskPath.empty())
      {
        if (PropertyButton("Add Mask Path", {
          .icon = ICON_LC_SPLINE,
          .tooltip = "Flattens the terrain along a path drawn top-down; the height rises with the distance from it." }))
        {
          terrain.maskPath = { { 0.5f, 0.0f }, { 0.5f, 1.0f } };
          edit.committed = true;
        }
      }
      else
      {
        if (BeginPropertyRow("Mask Path", { .tooltip = MASK_PATH_TOOLTIP.c_str() }))
        {
          const SplinePathEdit path = SplinePathEditor::Edit("Canvas", terrain.maskPath);
          edit.changed |= path.changed;
          edit.committed |= path.committed;
          EndPropertyRow();
        }

        edit |= PropertyFloat("Falloff Radius", terrain.maskFalloffRadius, {
          .min = 0.01f, .max = 1.0f, .speed = 0.005f, .format = "%.3f", .defaultValue = 0.5f,
          .tooltip = "Normalized 0-1 across the terrain: distance from the mask path at which the terrain reaches full "
                     "height." });

        if (terrain.maskCurve.empty())
        {
          if (PropertyButton("Add Falloff Curve", {
            .icon = ICON_LC_CHART_SPLINE,
            .tooltip = "Shapes the rise from the mask path to full height, which is linear without a curve." }))
          {
            terrain.maskCurve = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
            edit.committed = true;
          }
        }
        else
        {
          if (BeginPropertyRow("Falloff Curve", { .tooltip = FALLOFF_CURVE_TOOLTIP.c_str() }))
          {
            const CurveEdit curve = CurveEditor::Edit("Canvas", terrain.maskCurve);
            edit.changed |= curve.changed;
            edit.committed |= curve.committed;
            EndPropertyRow();
          }
          if (PropertyButton("Remove Falloff Curve", {
            .icon = ICON_LC_TRASH_2, .tooltip = "Goes back to a linear rise from the mask path to full height" }))
          {
            terrain.maskCurve.clear();
            edit.committed = true;
          }
        }

        if (PropertyButton("Remove Mask Path", {
          .icon = ICON_LC_TRASH_2, .tooltip = "Removes the mask path and its falloff curve: the noise covers the whole terrain" }))
        {
          terrain.maskPath.clear();
          terrain.maskCurve.clear();
          edit.committed = true;
        }
      }

      PopDependency();

      if (edit.committed)
        scene.GetRegistry().emplace_or_replace<TerrainDirty>(entity);
    }

    // Read by the renderer every frame, so edits apply live
    void DrawTerrainMaterialContents(TerrainMaterialComponent& material)
    {
      PropertySubHeading("Slope Layer");
      PropertyFloat("Slope Start", material.slopeStart, {
        .min = 0.0f, .max = 1.0f, .speed = 0.01f, .format = "%.2f", .defaultValue = 0.7f,
        .tooltip = "Normalized 0-1 steepness, 0 flat and 1 vertical, at which the slope layer starts to blend over the "
                   "base material." });
      PropertyFloat("Slope End", material.slopeEnd, {
        .min = 0.0f, .max = 1.0f, .speed = 0.01f, .format = "%.2f", .defaultValue = 0.85f,
        .tooltip = "Normalized 0-1 steepness at which the slope layer fully covers the base material." });
      PropertyFloat("Layer 1 UV Scale", material.layer1UvScale, {
        .min = 0.1f, .max = 100.0f, .speed = 0.1f, .format = "%.2f", .defaultValue = 8.0f,
        .tooltip = "Multiplies the terrain's UV Scale for the slope layer's textures." });

      PropertySubHeading("Shoulder Layer");
      PropertyFloat("Shoulder Outer Radius", material.shoulderOuterRadius, {
        .min = 0.0f, .max = 50.0f, .speed = 0.1f, .format = "%.2f", .unit = "m", .defaultValue = 0.0f,
        .tooltip = "Distance from the road's center line beyond which the shoulder layer is gone. 0 switches the "
                   "shoulder off." });

      PushDependency(material.shoulderOuterRadius > 0.0f, "Requires Shoulder Outer Radius above 0");
      PropertyFloat("Shoulder Inner Radius", material.shoulderInnerRadius, {
        .min = 0.0f, .max = 50.0f, .speed = 0.1f, .format = "%.2f", .unit = "m", .defaultValue = 0.0f,
        .tooltip = "Distance from the road's center line within which the shoulder layer fully covers the ground." });
      PropertyFloat("Layer 2 UV Scale", material.layer2UvScale, {
        .min = 0.1f, .max = 100.0f, .speed = 0.1f, .format = "%.2f", .defaultValue = 8.0f,
        .tooltip = "Multiplies the terrain's UV Scale for the shoulder layer's textures." });
      PropertyColor("Layer 2 Tint", material.layer2Tint, {
        .defaultValue = glm::vec3(1.0f), .tooltip = "Multiplies the shoulder layer's albedo." });
      PropertyFloat("Layer 2 Roughness", material.layer2RoughnessFactor, {
        .min = 0.0f, .max = 2.0f, .speed = 0.01f, .format = "%.2f", .defaultValue = 1.0f,
        .tooltip = "Multiplies the shoulder layer's roughness texture, or is the roughness without one." });
      PropertyFloat("Layer 2 Metallic", material.layer2MetallicFactor, {
        .min = 0.0f, .max = 2.0f, .speed = 0.01f, .format = "%.2f", .defaultValue = 0.0f,
        .tooltip = "Multiplies the shoulder layer's metallic texture, or is the metallic value without one." });
      PropertyFloat("Shoulder Warp Amplitude", material.shoulderWarpAmplitude, {
        .min = 0.0f, .max = 20.0f, .speed = 0.05f, .format = "%.2f", .unit = "m", .defaultValue = 0.0f,
        .tooltip = "How far the shoulder's edge wanders from a constant distance to the road. 0 keeps it even." });

      PushDependency(material.shoulderWarpAmplitude > 0.0f, "Requires Shoulder Warp Amplitude above 0");
      PropertyFloat("Shoulder Warp Scale", material.shoulderWarpScale, {
        .min = 0.001f, .max = 1.0f, .speed = 0.005f, .format = "%.3f", .defaultValue = 0.1f,
        .tooltip = "Frequency of the wandering edge: higher values give tighter wiggles." });
      PopDependency();
      PopDependency();
    }

    void DrawScatterContents(EditorContext& context, Entity entity, ScatterComponent& scatter,
      EditorCommands::EntityNameLookup& clusterSourceLookup)
    {
      Scene& scene = *context.scene;
      entt::registry& registry = scene.GetRegistry();
      const char* satelliteReason = scatter.clusterSource.empty() ? nullptr : SATELLITE_UNUSED;
      PropertyEdit edit;

      PropertySubHeading("Mesh");
      edit |= PropertyEnum("Mesh Type", scatter.meshType, SCATTER_MESH_TYPES, {
        .defaultValue = int32_t(ScatterMeshType::Plane), .tooltip = "What each instance renders." });
      const bool plane = scatter.meshType == ScatterMeshType::Plane;

      PushDependency(plane, "Plane mesh type only");
      edit |= PropertyFloat("Plane Width", scatter.planeWidth, {
        .min = 0.01f, .max = 10.0f, .speed = 0.01f, .format = "%.2f", .unit = "m", .defaultValue = 1.0f,
        .tooltip = "Width of each quad before the instance scale." });
      edit |= PropertyFloat("Plane Height", scatter.planeHeight, {
        .min = 0.01f, .max = 10.0f, .speed = 0.01f, .format = "%.2f", .unit = "m", .defaultValue = 1.0f,
        .tooltip = "Height of each quad before the instance scale; the quads stand on the ground." });
      const std::string texture = scatter.materialPath.empty() ? std::string("None") : context.assetManager->MakeRelative(scatter.materialPath);
      PropertyReadOnly("Texture", texture.c_str(), {
        .mono = true, .tooltip = "Base color of the quads; its alpha channel cuts them out." });
      if (PropertyButton("Load Texture...", {
        .icon = ICON_LC_FOLDER_OPEN, .tooltip = "Picks the image the quads are textured with" }))
      {
        const std::string path = FileDialog::OpenFile(SCATTER_TEXTURE_FILTERS, 1);
        if (!path.empty())
        {
          scatter.materialPath = path;
          edit.committed = true;
        }
      }
      PopDependency();

      PropertySubHeading("Placement");
      edit |= PropertyUInt("Count", scatter.count, {
        .min = 0, .max = Render::MAX_INSTANCES, .speed = 1.0f, .defaultValue = 50,
        .tooltip = "Placement attempts: spots that are too steep or masked out are skipped, so fewer instances may appear.",
        .disabledReason = satelliteReason });
      edit |= PropertyInt("Seed", scatter.seed, {
        .speed = 0.25f, .defaultValue = 0,
        .tooltip = "Shifts the random placement: each seed gives a different layout from the same settings." });
      edit |= PropertyFloat("Min Scale", scatter.minScale, {
        .min = 0.01f, .max = 10.0f, .speed = 0.01f, .format = "%.2f", .defaultValue = 0.8f,
        .tooltip = "Smallest random uniform scale of an instance." });
      edit |= PropertyFloat("Max Scale", scatter.maxScale, {
        .min = 0.01f, .max = 10.0f, .speed = 0.01f, .format = "%.2f", .defaultValue = 1.2f,
        .tooltip = "Largest random uniform scale of an instance." });
      edit |= PropertyFloat("Max Slope", scatter.maxSlope, {
        .min = 0.0f, .max = 1.0f, .speed = 0.01f, .format = "%.2f", .defaultValue = 0.8f,
        .tooltip = "Normalized 0-1: the smallest upward component of the ground normal an instance may stand on. Higher "
                   "values keep instances to flatter ground; 0 allows any slope." });
      edit |= PropertyBool("Random Y Rotation", scatter.randomYRotation, {
        .defaultValue = true, .tooltip = "Turns each instance to a random heading." });
      edit |= PropertyFloat("Radius", scatter.radius, {
        .min = 0.0f, .max = 10000.0f, .speed = 0.5f, .format = "%.1f", .unit = "m", .defaultValue = 0.0f,
        .tooltip = "Instances are placed within this distance of the terrain's center along X and Z; 0 covers the whole "
                   "terrain.",
        .disabledReason = satelliteReason });

      edit |= PropertyBool("Use Road Mask", scatter.useRoadMask, {
        .defaultValue = false,
        .tooltip = "Keeps instances to a band beside the roads: clear of the road and within Outer Radius of it.",
        .disabledReason = satelliteReason });
      PushDependency(scatter.useRoadMask, "Requires Use Road Mask");
      edit |= PropertyFloat("Road Padding", scatter.roadMaskPadding, {
        .min = 0.0f, .max = 50.0f, .speed = 0.1f, .format = "%.2f", .unit = "m", .defaultValue = 1.0f,
        .tooltip = "Clearance beyond the road's edge inside which nothing is placed.",
        .disabledReason = satelliteReason });
      edit |= PropertyFloat("Outer Radius", scatter.roadMaskOuterRadius, {
        .min = 0.0f, .max = 200.0f, .speed = 0.5f, .format = "%.1f", .unit = "m", .defaultValue = 20.0f,
        .tooltip = "Distance from the road's center line beyond which nothing is placed.",
        .disabledReason = satelliteReason });
      edit |= PropertyFloat("Falloff", scatter.roadMaskFalloff, {
        .min = 0.0f, .max = 50.0f, .speed = 0.1f, .format = "%.2f", .unit = "m", .defaultValue = 0.0f,
        .tooltip = "Width of the band inside Outer Radius over which instances thin out; 0 ends them abruptly.",
        .disabledReason = satelliteReason });
      PopDependency();

      PropertySubHeading("Clusters");
      // Stored as a name, resolved to an entity only for the picker, so the scene format stays as it was
      Entity source = clusterSourceLookup.Resolve(scene, scatter.clusterSource);
      const PropertyEdit sourceEdit = PropertyEntity("Cluster Source", source, scene, {
        .allowNone = true,
        .uniqueNames = true,
        .filter = [&registry, entity](Entity candidate) {
          const ScatterComponent* candidateScatter = registry.try_get<ScatterComponent>(candidate);
          return candidate != entity && candidateScatter != nullptr && candidateScatter->clusterSource.empty();
        },
        .tooltip = "Another scatter to cluster around: this one then places Cluster Count Min to Max instances within "
                   "Cluster Radius of each of the source's instances. None scatters over the terrain on its own. "
                   "Stored by the source's name, so a scatter whose name another entity shares cannot be picked." });
      if (sourceEdit.changed)
      {
        scatter.clusterSource = source != entt::null ? std::string(registry.get<Name>(source)) : std::string();
        source = clusterSourceLookup.Resolve(scene, scatter.clusterSource);
      }
      edit |= sourceEdit;

      if (!scatter.clusterSource.empty() && source == entt::null)
      {
        const std::string missing = "No entity is named '" + scatter.clusterSource + "'";
        PropertyStatus(nullptr, missing.c_str(), StatusKind::Warning,
          "The scatter looks its Cluster Source up by name when it regenerates and places nothing while the name matches "
          "no entity. Pick a source or None.");
      }
      else if (clusterSourceLookup.GetMatchCount() > 1)
      {
        const std::string ambiguous = std::to_string(clusterSourceLookup.GetMatchCount()) + " entities are named '"
          + scatter.clusterSource + "'";
        PropertyStatus(nullptr, ambiguous.c_str(), StatusKind::Warning,
          "The scatter clusters around whichever of them the scene lists first when it regenerates, which can change as "
          "entities are added or removed. Rename all but one of them, or pick another source.");
      }

      PushDependency(!scatter.clusterSource.empty(), "Requires a Cluster Source");
      edit |= PropertyFloat("Cluster Radius", scatter.clusterRadius, {
        .min = 0.5f, .max = 20.0f, .speed = 0.05f, .format = "%.2f", .unit = "m", .defaultValue = 3.0f,
        .tooltip = "Distance from each source instance within which this scatter's instances are placed." });
      edit |= PropertyUInt("Cluster Count Min", scatter.clusterCountMin, {
        .min = 0, .max = 1000, .speed = 0.1f, .defaultValue = 1,
        .tooltip = "Fewest instances placed around each source instance." });
      edit |= PropertyUInt("Cluster Count Max", scatter.clusterCountMax, {
        .min = 0, .max = 1000, .speed = 0.1f, .defaultValue = 3,
        .tooltip = "Most instances placed around each source instance; never below Cluster Count Min." });
      scatter.clusterCountMax = std::max(scatter.clusterCountMax, scatter.clusterCountMin);
      PopDependency();

      PropertySubHeading("Collider");
      edit |= DrawColliderProperties(ColliderFields {
        .enabled = &scatter.colliderEnabled, .enabledDefault = true,
        .offset = &scatter.colliderOffset, .halfExtents = &scatter.colliderHalfExtentsScale,
        .isStatic = &scatter.colliderIsStatic, .layer = &scatter.colliderLayer, .mask = &scatter.colliderMask },
        plane ? "Plane scatters build no colliders" : nullptr);

      if (edit.committed)
        registry.emplace_or_replace<ScatterDirty>(entity);
    }
  }

  const DetailsPanel::Section DetailsPanel::SECTIONS[] = {
    { .present = [](Scene& scene, Entity entity) { return scene.HasComponent<LocalTransform>(entity); },
      .draw = &DetailsPanel::DrawTransformSection },
    { .present = [](Scene& scene, Entity entity) { return scene.HasComponent<ModelSourceComponent>(entity); },
      .draw = &DetailsPanel::DrawModelSection },
    { .present = [](Scene& scene, Entity entity) { return scene.HasComponent<MaterialComponent>(entity); },
      .draw = &DetailsPanel::DrawMaterialSection },
    { .present = [](Scene& scene, Entity entity) { return scene.HasComponent<LightComponent>(entity); },
      .draw = &DetailsPanel::DrawLightSection },
    { .present = [](Scene& scene, Entity entity) { return scene.HasComponent<CameraComponent>(entity); },
      .draw = &DetailsPanel::DrawCameraSection },
    { .present = [](Scene& scene, Entity entity) { return scene.HasComponent<CameraTrackComponent>(entity); },
      .draw = &DetailsPanel::DrawCameraTrackSection },
    { .present = [](Scene& scene, Entity entity) { return scene.HasComponent<ReflectionProbeComponent>(entity); },
      .draw = &DetailsPanel::DrawReflectionProbeSection },
    { .present = [](Scene& scene, Entity entity) { return scene.HasComponent<IrradianceVolumeComponent>(entity); },
      .draw = &DetailsPanel::DrawIrradianceVolumeSection },
    { .present = &ShowsBakeInclusion,
      .draw = &DetailsPanel::DrawBakeInclusionSection },
    { .present = [](Scene& scene, Entity entity) { return scene.HasComponent<TerrainComponent>(entity); },
      .draw = &DetailsPanel::DrawTerrainSection },
    { .present = [](Scene& scene, Entity entity) { return scene.HasComponent<TerrainMaterialComponent>(entity); },
      .draw = &DetailsPanel::DrawTerrainMaterialSection },
    { .present = [](Scene& scene, Entity entity) { return scene.HasComponent<RoadComponent>(entity); },
      .draw = &DetailsPanel::DrawRoadSection },
    { .present = [](Scene& scene, Entity entity) { return scene.HasComponent<ScatterComponent>(entity); },
      .draw = &DetailsPanel::DrawScatterSection },
    { .present = [](Scene& scene, Entity entity) { return scene.HasComponent<ColliderComponent>(entity); },
      .draw = &DetailsPanel::DrawColliderSection },
  };

  void DetailsPanel::LinkPanels(MaterialInspectorPanel& materialInspector, SequencerPanel& sequencer, ShowPanelFunction showPanel)
  {
    m_MaterialInspector = &materialInspector;
    m_Sequencer = &sequencer;
    m_ShowPanel = std::move(showPanel);
  }

  void DetailsPanel::DrawHeader(EditorContext& context, Entity entity)
  {
    Scene& scene = *context.scene;
    entt::registry& registry = scene.GetRegistry();
    const ImGuiStyle& style = ImGui::GetStyle();

    const Name* name = registry.try_get<Name>(entity);
    if (!b_NameEditing || m_NameEditEntity != entity)
    {
      b_NameEditing = false;
      m_NameBuffer = name != nullptr ? *name : std::string();
    }

    const std::string displayName = EditorCommands::GetEntityDisplayName(registry, entity);
    const float fieldWidth = std::max(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - style.ItemInnerSpacing.x, 1.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    // The label is the field's id and bridge label; it is drawn past the frame, where the clip rect hides it
    ImGui::PushClipRect(pos, ImVec2(pos.x + fieldWidth, pos.y + ImGui::GetFrameHeight()), true);
    ImGui::SetNextItemWidth(fieldWidth);
    ImGui::InputTextWithHint("Name", displayName.c_str(), &m_NameBuffer);
    if (ImGui::IsItemActivated())
    {
      b_NameEditing = true;
      m_NameEditEntity = entity;
    }
    else if (ImGui::IsItemDeactivated())
    {
      // A field that lost focus to a click selecting another entity still belongs to the old one; that
      // edit is dropped instead of renaming the new selection
      if (b_NameEditing && m_NameEditEntity == entity && !m_NameBuffer.empty())
        EditorCommands::RenameEntity(scene, entity, m_NameBuffer);
      b_NameEditing = false;
    }
    ImGui::SetItemTooltip("Entity name. Applied on Enter or when the field loses focus; an empty name is rejected.");
    ImGui::PopClipRect();

    ImGui::SetCursorScreenPos(ImVec2(pos.x + fieldWidth + style.ItemInnerSpacing.x, pos.y));
    if (InlineButton("Show in Outliner", {
      .icon = ICON_LC_LIST_TREE, .iconOnly = true,
      .tooltip = "Expands the Outliner down to this entity and scrolls it into view" }))
    {
      context.RevealEntity(entity);
    }

    // Model subtree entities are rebuilt from the model file on every load, so anything authored on
    // them is stored as an override. Surface that state and let it be undone.
    const ModelNodeComponent* node = registry.try_get<ModelNodeComponent>(entity);
    if (node == nullptr || node->nodeIndex == 0 || context.componentRegistry == nullptr || context.assetManager == nullptr)
      return;

    AssetManager& assets = *context.assetManager;
    ComponentRegistry& components = *context.componentRegistry;
    const bool nodeOverridden = ModelOverrides::IsNodeOverridden(scene, assets, components, entity);
    const bool materialOverridden = ModelOverrides::IsMaterialOverridden(scene, assets, entity);

    BeginPropertyScope();
    PropertyStatus("Model Node", nodeOverridden ? "Overridden" : "Matches the model file",
      nodeOverridden ? StatusKind::Warning : StatusKind::Neutral,
      "Entities of an imported model are rebuilt from the model file on every load; edits made to them are saved "
      "with the scene as overrides.");
    EndPropertyScope();

    const float half = std::max((ImGui::GetContentRegionAvail().x - style.ItemInnerSpacing.x) * 0.5f, 1.0f);
    if (InlineButton("Revert Node", {
      .icon = ICON_LC_UNDO_2, .width = half,
      .tooltip = "Drops every override of this node, its material included, and restores it from the model file",
      .disabledReason = nodeOverridden ? nullptr : "The node matches the model file" }))
    {
      ModelOverrides::RevertNode(scene, assets, components, entity);
    }
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    if (InlineButton("Revert Material", {
      .icon = ICON_LC_UNDO_2, .width = -1.0f,
      .tooltip = "Restores this node's material slot from the model file",
      .disabledReason = materialOverridden ? nullptr : "The material matches the model file" }))
    {
      ModelOverrides::RevertMaterial(scene, assets, entity);
    }
  }

  void DetailsPanel::DrawTransformSection(EditorContext& context, Entity entity)
  {
    if (!BeginSection("Transform", ICON_LC_MOVE, "Position, rotation and scale relative to the parent entity"))
      return;

    Scene& scene = *context.scene;
    LocalTransform& transform = scene.GetTransform(entity);

    PropertyEdit edit = PropertyVec3("Position", transform.position, {
      .speed = 0.05f, .format = "%.2f", .unit = "m", .defaultValue = glm::vec3(0.0f),
      .tooltip = "Relative to the parent entity; world space for an entity without a parent." });

    const bool cached = m_EulerEntity == entity && SameRotation(m_EulerRotation, transform.rotation);
    glm::vec3 euler = cached ? m_EulerDegrees : glm::degrees(glm::eulerAngles(transform.rotation));
    const PropertyEdit rotation = PropertyVec3("Rotation", euler, {
      .speed = 0.5f, .format = "%.1f", .unit = "deg", .defaultValue = glm::vec3(0.0f),
      .tooltip = "Euler angles around X, Y and Z, relative to the parent entity. Typed values stay as typed while nothing "
                 "else rotates the entity." });
    if (rotation.changed)
      transform.rotation = glm::quat(glm::radians(euler));
    m_EulerEntity = entity;
    m_EulerRotation = transform.rotation;
    m_EulerDegrees = euler;
    edit |= rotation;

    edit |= PropertyVec3("Scale", transform.scale, {
      .speed = 0.01f, .min = 0.001f, .max = 1000.0f, .format = "%.3f", .defaultValue = glm::vec3(1.0f),
      .tooltip = "Size multiplier along each local axis, relative to the parent entity." });

    if (edit.changed)
      scene.MarkDirty(entity);

    EndPropertyGroup();
  }

  void DetailsPanel::DrawModelSection(EditorContext& context, Entity entity)
  {
    if (!BeginSection("Model", ICON_LC_FILE_INPUT, "The model file this entity and its children are built from"))
      return;

    Scene& scene = *context.scene;
    ModelSourceComponent& model = scene.GetComponent<ModelSourceComponent>(entity);

    const std::string relativePath = context.assetManager->MakeRelative(model.path);
    PropertyReadOnly("File", relativePath.c_str(), { .tooltip = model.path.c_str() });

    if (PropertyBool("Combined Textures", model.combinedTextures, {
      .tooltip = "Whether the model's materials read roughness from the green channel of their metallic texture (glTF "
                 "packed metallic-roughness)." }).changed)
    {
      ModelOverrides::SetCombinedTextures(scene, *context.assetManager, entity, model.combinedTextures);
    }

    PropertySubHeading("Collider");
    if (DrawColliderProperties(ColliderFields {
      .enabled = &model.colliderEnabled, .enabledDefault = false,
      .offset = &model.colliderOffset, .halfExtents = &model.colliderHalfExtentsScale,
      .isStatic = &model.colliderIsStatic, .layer = &model.colliderLayer, .mask = &model.colliderMask }, nullptr).committed)
    {
      scene.GetRegistry().emplace_or_replace<ModelColliderDirty>(entity);
    }

    EndPropertyGroup();
  }

  void DetailsPanel::DrawMaterialSection(EditorContext& context, Entity entity)
  {
    if (!BeginSection("Material", ICON_LC_PALETTE, "The material this entity renders with"))
      return;

    Scene& scene = *context.scene;
    MaterialComponent& component = scene.GetComponent<MaterialComponent>(entity);
    auto& materials = context.assetManager->Materials();
    const Material* current = materials.TryGet(component.asset);

    if (BeginPropertyRow("Material"))
    {
      const ImGuiStyle& style = ImGui::GetStyle();
      const float comboWidth = std::max(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - style.ItemInnerSpacing.x, 1.0f);
      const ImVec2 pos = ImGui::GetCursorScreenPos();

      // The label is the picker's id and bridge label; it is drawn past the frame, where the clip rect hides it
      ImGui::PushClipRect(pos, ImVec2(pos.x + comboWidth, pos.y + ImGui::GetFrameHeight()), true);
      ImGui::SetNextItemWidth(comboWidth);
      if (ImGui::BeginCombo("Picker", current != nullptr ? current->name.c_str() : "None", ImGuiComboFlags_HeightLargest))
      {
        MaterialHandle picked = DrawMaterialList(materials, component.asset, m_MaterialSearch, sizeof(m_MaterialSearch));
        if (picked)
        {
          component.asset = picked;
          context.SelectMaterial(picked);
        }
        ImGui::EndCombo();
      }
      ImGui::SetItemTooltip("Pick one of the scene's materials; type in the list to search by name.");
      ImGui::PopClipRect();

      ImGui::SetCursorScreenPos(ImVec2(pos.x + comboWidth + style.ItemInnerSpacing.x, pos.y));
      if (InlineButton("Edit", {
        .icon = ICON_LC_SQUARE_PEN, .iconOnly = true,
        .tooltip = "Opens this material in the Material Inspector",
        .disabledReason = materials.TryGet(component.asset) != nullptr ? nullptr : "No material assigned" }))
      {
        context.SelectMaterial(component.asset);
        if (m_MaterialInspector != nullptr && m_ShowPanel)
          m_ShowPanel(*m_MaterialInspector);
      }
      EndPropertyRow();
    }

    if (materials.TryGet(component.asset) != nullptr)
    {
      const size_t users = m_MaterialUsers.Get(scene, component.asset, ImGui::GetTime());
      char usage[64];
      if (users > 1)
        std::snprintf(usage, sizeof(usage), "Shared by %zu entities", users);
      else
        std::snprintf(usage, sizeof(usage), "Used only by this entity");
      PropertyStatus(nullptr, usage, StatusKind::Neutral,
        "Edits in the Material Inspector change every entity that uses this material.");
    }

    EndPropertyGroup();
  }

  void DetailsPanel::DrawLightSection(EditorContext& context, Entity entity)
  {
    bool remove = false;
    if (BeginSection("Light", ICON_LC_LIGHTBULB, "Point, spot or directional light", &remove))
    {
      LightComponent& light = context.scene->GetComponent<LightComponent>(entity);

      PropertyEnum("Type", light.type, LIGHT_TYPES, {
        .defaultValue = int32_t(LightType::Point), .tooltip = "How the light is emitted." });

      const bool ranged = light.type != LightType::Directional;
      const bool spot = light.type == LightType::Spot;

      PropertyColor("Color", light.color, { .defaultValue = glm::vec3(1.0f), .tooltip = "Color of the emitted light." });
      PropertyFloat("Intensity", light.intensity, {
        .min = 0.0f, .max = FLT_MAX, .speed = 0.1f, .format = "%.2f", .defaultValue = 1.0f,
        .tooltip = "Brightness multiplier on Color." });
      PropertyFloat("Radius", light.radius, {
        .min = 0.0f, .max = FLT_MAX, .speed = 0.5f, .format = "%.2f", .unit = "m", .defaultValue = 10.0f,
        .tooltip = "Distance at which the light's falloff reaches zero; the light is culled beyond it.",
        .disabledReason = ranged ? nullptr : "Point and Spot lights only: a directional light has no position" });

      float innerDegrees = glm::degrees(light.innerCone);
      if (PropertyFloat("Inner Cone", innerDegrees, {
        .min = 0.0f, .max = 90.0f, .speed = 0.25f, .format = "%.1f", .unit = "deg", .slider = true, .defaultValue = 25.0f,
        .tooltip = "Angle from the spot axis within which the light shines at full intensity. It fades out between "
                   "Inner Cone and Outer Cone.",
        .disabledReason = spot ? nullptr : "Spot lights only" }).changed)
      {
        light.innerCone = glm::radians(innerDegrees);
      }

      float outerDegrees = glm::degrees(light.outerCone);
      if (PropertyFloat("Outer Cone", outerDegrees, {
        .min = 0.0f, .max = 90.0f, .speed = 0.25f, .format = "%.1f", .unit = "deg", .slider = true, .defaultValue = 35.0f,
        .tooltip = "Angle from the spot axis at which the light reaches zero.",
        .disabledReason = spot ? nullptr : "Spot lights only" }).changed)
      {
        light.outerCone = glm::radians(outerDegrees);
      }

      PropertyBool("Cast Shadows", light.castShadow, {
        .defaultValue = false,
        .tooltip = "Renders a shadow map for this light. Shadows in Render Settings switches every shadow map off." });

      PushDependency(light.castShadow, "Requires Cast Shadows");
      PropertyFloat("Shadow Distance", light.shadowDistance, {
        .min = 1.0f, .max = 5000.0f, .speed = 1.0f, .format = "%.1f", .unit = "m", .defaultValue = 200.0f,
        .tooltip = "How far from the camera the cascaded shadow maps reach. Capped by the camera's View Distance.",
        .disabledReason = light.type == LightType::Directional ? nullptr : "Directional lights only" });
      PopDependency();

      PropertyFloat("Source Radius", light.sourceRadius, {
        .min = 0.0f, .max = FLT_MAX, .speed = 0.01f, .format = "%.3f", .unit = "m", .defaultValue = 0.0f,
        .tooltip = "Radius of the emitting sphere. Softens path traced and baked shadows without changing brightness; "
                   "0 is a point source. Raster lighting ignores it.",
        .disabledReason = ranged ? nullptr : "Point and Spot lights only" });
      PropertyFloat("Angular Diameter", light.angularDiameter, {
        .min = 0.0f, .max = 90.0f, .speed = 0.01f, .format = "%.2f", .unit = "deg", .defaultValue = 0.53f,
        .tooltip = "Apparent size of the sun disk, 0.53 for the real sun. Softens path traced and baked shadows without "
                   "changing brightness; 0 is a point source. Raster lighting ignores it.",
        .disabledReason = light.type == LightType::Directional ? nullptr : "Directional lights only" });
      PropertyBool("Raster Only", light.rasterOnly, {
        .defaultValue = false,
        .tooltip = "Ignored by the path tracer and the probe baker, which treat this light as absent. Meant for lights "
                   "standing in for emissive geometry: those two already light the scene with the emitter itself." });

      EndPropertyGroup();
    }

    if (remove)
      context.scene->RemoveComponent<LightComponent>(entity);
  }

  void DetailsPanel::DrawCameraSection(EditorContext& context, Entity entity)
  {
    Scene& scene = *context.scene;
    const bool editorCamera = scene.HasComponent<EditorOnlyTag>(entity);
    const char* removeBlocked = editorCamera ? "The editor camera cannot be removed"
      : scene.HasComponent<CameraTrackComponent>(entity) ? "Remove the Camera Track first: its playback drives this camera"
      : scene.GetActiveCamera() == entity && context.previewCamera != entity ? "The viewport is looking through this camera"
      : nullptr;

    bool remove = false;
    if (BeginSection("Camera", ICON_LC_VIDEO, "Scene camera: field of view and clip planes", &remove, removeBlocked))
    {
      CameraComponent& camera = scene.GetComponent<CameraComponent>(entity);

      float fovDegrees = glm::degrees(camera.fov);
      if (PropertyFloat("FOV", fovDegrees, {
        .min = 10.0f, .max = 120.0f, .speed = 0.25f, .format = "%.1f", .unit = "deg", .defaultValue = 58.31f,
        .tooltip = "Vertical field of view." }).changed)
      {
        camera.fov = glm::radians(fovDegrees);
      }
      PropertyFloat("Near Plane", camera.nearPlane, {
        .min = 0.01f, .max = 10.0f, .speed = 0.01f, .format = "%.3f", .unit = "m", .defaultValue = 0.1f,
        .tooltip = "Closest distance the camera renders." });
      PropertyFloat("View Distance", camera.farPlane, {
        .min = 10.0f, .max = 10000.0f, .speed = 1.0f, .format = "%.1f", .unit = "m", .defaultValue = 1000.0f,
        .tooltip = "Far clip plane: nothing farther is rendered. It also caps a directional light's Shadow Distance." });

      if (!editorCamera)
      {
        SuspendPropertyGrid();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float half = std::max((ImGui::GetContentRegionAvail().x - style.ItemInnerSpacing.x) * 0.5f, 1.0f);
        if (context.previewCamera == entity)
        {
          if (InlineButton("Stop Preview", {
            .icon = ICON_LC_CIRCLE_STOP, .width = half,
            .tooltip = "Hands the viewport back to the camera it showed before the preview" }))
          {
            context.StopCameraPreview();
          }
        }
        else if (InlineButton("Preview Camera", {
          .icon = ICON_LC_SCAN_EYE, .width = half,
          .tooltip = "Shows the viewport through this camera until Stop Preview" }))
        {
          context.StartCameraPreview(entity);
        }

        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        if (InlineButton("Set To View", {
          .icon = ICON_LC_LOCATE_FIXED, .width = -1.0f,
          .tooltip = "Moves this camera to the editor camera's position and rotation" }))
        {
          SetCameraToEditorView(scene, entity);
        }
      }

      EndPropertyGroup();
    }

    if (remove)
    {
      if (context.previewCamera == entity)
        context.StopCameraPreview();
      scene.RemoveComponent<CameraComponent>(entity);
    }
  }

  void DetailsPanel::DrawCameraTrackSection(EditorContext& context, Entity entity)
  {
    Scene& scene = *context.scene;
    bool remove = false;
    if (BeginSection("Camera Track", ICON_LC_FILM, "Authored camera move; its keys are edited in the Sequencer", &remove))
    {
      const CameraTrackComponent& track = scene.GetComponent<CameraTrackComponent>(entity);

      char keys[32];
      std::snprintf(keys, sizeof(keys), "%zu", track.keys.size());
      PropertyReadOnly("Keys", keys, { .mono = true, .tooltip = "Keyframes on the track" });

      char duration[32];
      std::snprintf(duration, sizeof(duration), "%.2f s", track.keys.empty() ? 0.0 : double(track.keys.back().time));
      PropertyReadOnly("Duration", duration, { .mono = true, .tooltip = "Time of the last key" });

      if (PropertyButton("Open in Sequencer", {
        .icon = ICON_LC_CLAPPERBOARD,
        .tooltip = "Opens the Sequencer on this track",
        .disabledReason = m_Sequencer != nullptr && m_ShowPanel ? nullptr : "The Sequencer is not available" }))
      {
        m_ShowPanel(*m_Sequencer);
      }

      EndPropertyGroup();
    }

    if (remove)
      scene.RemoveComponent<CameraTrackComponent>(entity);
  }

  void DetailsPanel::DrawReflectionProbeSection(EditorContext& context, Entity entity)
  {
    Scene& scene = *context.scene;
    bool remove = false;
    if (BeginSection("Reflection Probe", ICON_LC_GLOBE, "Baked specular reflections for the surfaces inside its volume", &remove))
    {
      ReflectionProbeComponent& probe = scene.GetComponent<ReflectionProbeComponent>(entity);

      PropertySubHeading("Volume");
      PropertyEnum("Shape", probe.shape, PROBE_SHAPES, {
        .defaultValue = int32_t(ProbeShape::Sphere),
        .tooltip = "Form of the influence volume: surfaces inside it use this probe." });

      if (probe.shape == ProbeShape::Sphere)
      {
        PropertyFloat("Radius", probe.extents.x, {
          .min = 0.1f, .max = 1000.0f, .speed = 0.1f, .format = "%.2f", .unit = "m", .defaultValue = 5.0f,
          .tooltip = "Radius of the influence sphere around the entity. Transform scale is ignored." });
        probe.extents.y = probe.extents.x;
        probe.extents.z = probe.extents.x;
      }
      else
      {
        PropertyVec3("Extents", probe.extents, {
          .speed = 0.1f, .min = 0.1f, .max = 1000.0f, .format = "%.2f", .unit = "m", .defaultValue = glm::vec3(5.0f),
          .tooltip = "Half size of the influence box along each local axis. The box takes its rotation from the entity "
                     "transform (rotate gizmo, key 2); transform scale is ignored." });
      }

      PropertyBool("Parallax Correction", probe.parallaxCorrection, {
        .defaultValue = false,
        .tooltip = "Reprojects reflections onto the proxy volume. Only correct while the volume matches the real geometry." });

      PushDependency(probe.parallaxCorrection, "Requires Parallax Correction");
      // Seeded from the influence volume so switching the override on is a no-op until the values are
      // actually moved - otherwise the reflection jumps
      if (PropertyBool("Custom Proxy Volume", probe.customProxyVolume, {
        .defaultValue = false,
        .tooltip = "Gives parallax its own volume, separate from the influence volume, so a thin influence slab can "
                   "reproject onto a deep proxy box." }).changed && probe.customProxyVolume)
      {
        probe.proxyOffset = glm::vec3(0.0f);
        probe.proxyExtents = probe.extents;
      }

      PushDependency(probe.customProxyVolume, "Requires Custom Proxy Volume");
      PropertyVec3("Proxy Offset", probe.proxyOffset, {
        .speed = 0.1f, .min = -1000.0f, .max = 1000.0f, .format = "%.2f", .unit = "m", .defaultValue = glm::vec3(0.0f),
        .tooltip = "Proxy centre relative to the probe, in probe local space. Drawn in green by the probe volume gizmo." });
      if (probe.shape == ProbeShape::Sphere)
      {
        PropertyFloat("Proxy Radius", probe.proxyExtents.x, {
          .min = 0.1f, .max = 1000.0f, .speed = 0.1f, .format = "%.2f", .unit = "m",
          .tooltip = "Radius of the parallax proxy sphere. Drawn in green by the probe volume gizmo." });
        if (probe.parallaxCorrection && probe.customProxyVolume)
        {
          probe.proxyExtents.y = probe.proxyExtents.x;
          probe.proxyExtents.z = probe.proxyExtents.x;
        }
      }
      else
      {
        PropertyVec3("Proxy Extents", probe.proxyExtents, {
          .speed = 0.1f, .min = 0.1f, .max = 1000.0f, .format = "%.2f", .unit = "m",
          .tooltip = "Half size of the parallax proxy box. Drawn in green by the probe volume gizmo." });
      }
      PopDependency();
      PopDependency();

      PropertySubHeading("Blending");
      PropertyFloat("Fade Distance", probe.fadeDistance, {
        .min = 0.0f, .max = 100.0f, .speed = 0.1f, .format = "%.2f", .unit = "m", .defaultValue = 1.0f,
        .tooltip = "Width of the band inside the volume's edge over which the probe fades out." });
      PropertyInt("Priority", probe.priority, {
        .min = -100, .max = 100, .speed = 0.1f, .defaultValue = 0,
        .tooltip = "Orders overlapping probes: higher values take precedence." });

      PropertySubHeading("Bake");
      // Options come from the baker range, so the list can never offer a value the bake path would clamp away
      char resolutionLabels[MAX_RESOLUTION_OPTIONS][8];
      EnumOption resolutionOptions[MAX_RESOLUTION_OPTIONS];
      uint32_t resolutionValues[MAX_RESOLUTION_OPTIONS];
      size_t resolutionCount = 0;
      int32_t resolutionIndex = -1;
      int32_t resolutionDefault = 0;
      for (uint32_t option = BakeLimits::PROBE_MIN_CAPTURE_RESOLUTION;
           option <= BakeLimits::PROBE_MAX_CAPTURE_RESOLUTION && resolutionCount < MAX_RESOLUTION_OPTIONS; option *= 2)
      {
        std::snprintf(resolutionLabels[resolutionCount], sizeof(resolutionLabels[resolutionCount]), "%u", option);
        resolutionOptions[resolutionCount] = EnumOption { .label = resolutionLabels[resolutionCount] };
        resolutionValues[resolutionCount] = option;
        if (probe.resolution == option)
          resolutionIndex = int32_t(resolutionCount);
        if (option == BakeLimits::PROBE_DEFAULT_CAPTURE_RESOLUTION)
          resolutionDefault = int32_t(resolutionCount);
        resolutionCount++;
      }
      if (PropertyEnum("Resolution", resolutionIndex, std::span<const EnumOption>(resolutionOptions, resolutionCount), {
        .defaultValue = resolutionDefault,
        .tooltip = "Pixels along each face of the captured cubemap. Higher is sharper and slower to bake." }).changed)
      {
        probe.resolution = resolutionValues[size_t(resolutionIndex)];
      }

      if (PropertyButton("Bake Probe", {
        .icon = ICON_LC_CIRCLE_PLAY,
        .tooltip = "Captures and prefilters this probe now; the editor stalls until the bake ends. Bake All Reflection "
                   "Probes (Render Settings > Baking) bakes every probe, with bounces between them." }))
      {
        context.render->BakeProbe(entity, scene, *context.assetManager);
      }

      const ReflectionProbeComponent& baked = scene.GetComponent<ReflectionProbeComponent>(entity);
      PropertyStatus("Status", baked.baked ? "Baked" : "Not baked", baked.baked ? StatusKind::Success : StatusKind::Warning,
        baked.baked && !baked.bakedPrefilterPath.empty() ? baked.bakedPrefilterPath.c_str()
          : "No baked cubemap yet: Bake Probe captures one. Developer > Reflection Probe Diagnostics previews it.");

      EndPropertyGroup();
    }

    if (remove)
      scene.RemoveComponent<ReflectionProbeComponent>(entity);
  }

  void DetailsPanel::DrawIrradianceVolumeSection(EditorContext& context, Entity entity)
  {
    Scene& scene = *context.scene;
    Render& render = *context.render;
    bool remove = false;
    if (BeginSection("Irradiance Volume", ICON_LC_BOXES, "Baked diffuse lighting for the space inside its box", &remove))
    {
      IrradianceVolumeComponent& volume = scene.GetComponent<IrradianceVolumeComponent>(entity);
      const bool bakeAvailable = render.IsRayTracedBakeAvailable();

      PropertySubHeading("Bounds");
      // Must be the rotation the baker extracts, or the tooltip disagrees with the bake
      const glm::quat boxRotation = ExtractIrradianceBoxRotation(scene.GetWorldTransform(entity).world);
      const bool rotated = std::abs(boxRotation.w) < 0.9999f;
      PropertyVec3("Half Extents", volume.halfExtents, {
        .speed = 0.1f, .min = 0.1f, .max = 1000.0f, .format = "%.2f", .unit = "m", .defaultValue = glm::vec3(5.0f),
        .tooltip = rotated
          ? "Half size of the box along each local axis. Position and rotation come from the entity transform; transform "
            "scale is ignored. The box is rotated: bricks stay world axis aligned, so covering it takes more of them than "
            "an unrotated box would need."
          : "Half size of the box along each local axis. Position and rotation come from the entity transform; transform "
            "scale is ignored." });

      PropertySubHeading("Placement");
      // Only powers of two from the shared set: that is what makes the nodes of a coarse volume a subset of a
      // finer one on the world lattice. A value from a hand-edited scene or from code is pulled onto the set
      // here, so what the lists show is also what gets saved.
      volume.minSpacing = SnapIrradianceSpacing(volume.minSpacing);
      volume.maxSpacing = std::max(SnapIrradianceSpacing(volume.maxSpacing), volume.minSpacing);
      int32_t minSpacingIndex = FindSpacingIndex(volume.minSpacing);
      int32_t maxSpacingIndex = FindSpacingIndex(volume.maxSpacing);

      if (PropertyEnum("Min Spacing", minSpacingIndex, SPACING_OPTIONS, {
        .defaultValue = FindSpacingIndex(0.5f),
        .tooltip = "Finest node spacing, reached next to geometry. Nodes sit on one world lattice shared by every volume, "
                   "so overlapping volumes agree; the box itself is never snapped, bricks on the lattice cover it." }).changed)
      {
        volume.minSpacing = IRRADIANCE_SPACINGS[size_t(minSpacingIndex)];
        volume.maxSpacing = std::max(volume.maxSpacing, volume.minSpacing);
      }
      if (PropertyEnum("Max Spacing", maxSpacingIndex, SPACING_OPTIONS, {
        .defaultValue = FindSpacingIndex(4.0f),
        .tooltip = "Coarsest node spacing, kept in open air." }).changed)
      {
        volume.maxSpacing = IRRADIANCE_SPACINGS[size_t(maxSpacingIndex)];
        volume.minSpacing = std::min(volume.minSpacing, volume.maxSpacing);
      }

      PropertyFloat("Backface Threshold", volume.backfaceRatioThreshold, {
        .min = BakeLimits::VOLUME_MIN_BACKFACE_THRESHOLD, .max = BakeLimits::VOLUME_MAX_BACKFACE_THRESHOLD,
        .speed = 0.01f, .format = "%.2f", .defaultValue = BakeLimits::VOLUME_DEFAULT_BACKFACE_THRESHOLD,
        .tooltip = "Normalized 0-1. Fraction of a node's probe rays that may hit the inside of single-sided geometry before "
                   "the bake rejects the node as buried behind a wall or under the ground. Rejected nodes are filled from "
                   "their nearest valid neighbours along the lattice axes, inverse distance weighted, in waves outward. "
                   "Lower catches more leaks, higher keeps more nodes; 1.00 keeps every node." });

      PropertyBool("Virtual Offset", volume.virtualOffset, {
        .defaultValue = true,
        .tooltip = "Buried nodes within their spacing of a back face are moved past that face and integrated again instead "
                   "of taking their neighbours' light. An offset node that is still buried, too close to geometry or "
                   "enclosed is dilated as before. Integrates those nodes a second time. Applies on the next bake." });

      char biasTooltip[512];
      std::snprintf(biasTooltip, sizeof(biasTooltip), "Extra clearance beyond the minimum probe clearance, %.2f of the "
        "node's spacing, at which a virtual offset node is placed in front of its nearest back face. The whole offset, face "
        "distance plus clearance plus bias, stays within the node's spacing: a node that would need more is dilated "
        "instead. Applies on the next bake.", double(BakeLimits::VOLUME_MIN_PROBE_CLEARANCE_FRACTION));
      PushDependency(volume.virtualOffset, "Requires Virtual Offset");
      PropertyFloat("Virtual Offset Bias", volume.virtualOffsetBias, {
        .min = BakeLimits::VOLUME_MIN_VIRTUAL_OFFSET_BIAS, .max = BakeLimits::VOLUME_MAX_VIRTUAL_OFFSET_BIAS,
        .speed = 0.001f, .format = "%.3f", .unit = "m", .defaultValue = BakeLimits::VOLUME_DEFAULT_VIRTUAL_OFFSET_BIAS,
        .tooltip = biasTooltip });
      PopDependency();

      if (PropertyButton("Preview Placement", {
        .icon = ICON_LC_GRID_3X3,
        .tooltip = "Lays out the sparse bricks the volume would bake with: they refine from Max Spacing down to Min Spacing "
                   "where ray traced queries find geometry near. Nothing is baked. The viewport's Show menu draws the "
                   "bricks.",
        .disabledReason = bakeAvailable ? nullptr : PREVIEW_UNAVAILABLE }))
      {
        render.PreviewIrradianceVolumePlacement(entity, scene, *context.assetManager);
      }
      DrawPlacementSummary(scene, render, entity);

      PropertySubHeading("Bake");
      PropertyFloat("Edge Fade", volume.edgeFade, {
        .min = BakeLimits::VOLUME_MIN_EDGE_FADE, .max = BakeLimits::VOLUME_MAX_EDGE_FADE,
        .speed = 0.01f, .format = "%.2f", .unit = "m", .defaultValue = BakeLimits::VOLUME_DEFAULT_EDGE_FADE,
        .tooltip = "Width over which the volume blends into the volume enclosing it, or into the sky, at its box faces. "
                   "Wide suits faces in open air and seams between nested volumes; narrow keeps the outside from reaching "
                   "into a box fitted to walls. Stored in the baked file: a change applies on the next bake." });

      if (PropertyButton("Bake Volume", {
        .icon = ICON_LC_CIRCLE_PLAY,
        .tooltip = "Bakes this volume by ray tracing now; the editor stalls until the bake ends. Bake time scales with "
                   "the nodes times Volume Samples, as the estimate below shows.",
        .disabledReason = bakeAvailable ? nullptr : VOLUME_BAKE_UNAVAILABLE }))
      {
        context.volumeBakeRequest = entity;
      }
      DrawBakeEstimate(scene, render, entity);

      IrradianceVolumeComponent& baked = scene.GetComponent<IrradianceVolumeComponent>(entity);
      // The test the scene load warns with, against the box the uploaded data was baked in
      glm::vec3 bakedPosition(0.0f);
      glm::vec3 bakedHalfExtents(0.0f);
      const bool outOfDate = baked.baked && render.GetBakedIrradianceVolumeBox(baked.atlasSlot, bakedPosition, bakedHalfExtents)
        && !MatchesBakedIrradianceBox(glm::vec3(scene.GetWorldTransform(entity).world[3]), baked.halfExtents,
          bakedPosition, bakedHalfExtents);
      if (outOfDate)
      {
        PropertyStatus("Status", "Baked, out of date - rebake", StatusKind::Warning,
          "The volume moved or was resized since it was baked, and its lighting still fills the box it was baked in. "
          "Bake Volume captures the current box.");
      }
      else
      {
        PropertyStatus("Status", baked.baked ? "Baked" : "Not baked", baked.baked ? StatusKind::Success : StatusKind::Warning,
          baked.baked && !baked.bakedVolumePath.empty() ? baked.bakedVolumePath.c_str()
            : "No baked volume yet: Bake Volume creates one.");
      }

      EndPropertyGroup();
    }

    if (remove)
      scene.RemoveComponent<IrradianceVolumeComponent>(entity);
  }

  void DetailsPanel::DrawBakeInclusionSection(EditorContext& context, Entity entity)
  {
    if (!BeginSection("Bake Inclusion", ICON_LC_COOKING_POT,
      "Whether reflection probe and irradiance volume bakes capture this entity"))
    {
      return;
    }

    Scene& scene = *context.scene;
    entt::registry& registry = scene.GetRegistry();

    // Scatter output is regenerated and never serialized, so an override set on it would silently
    // vanish on the next regeneration
    const bool scatterInstance = registry.all_of<ScatterInstanceTag>(entity);
    const BakeOverrideComponent* component = registry.try_get<BakeOverrideComponent>(entity);
    BakeOverride mode = component != nullptr ? component->mode : BakeOverride::Auto;

    // The effective state walks the hierarchy, so the text is rebuilt only when something it reads may have
    // changed; colliders and overrides further up change no generation, hence the refresh interval
    constexpr double BAKE_TOOLTIP_REFRESH_SECONDS = 0.5;
    const double now = ImGui::GetTime();
    const bool hidden = registry.all_of<HiddenTag>(entity);
    if (m_BakeTooltipEntity != entity || m_BakeTooltipMode != int32_t(mode) || b_BakeTooltipHidden != hidden
      || m_BakeTooltipGeneration != scene.GetStructureGeneration()
      || now < m_BakeTooltipTime || now - m_BakeTooltipTime >= BAKE_TOOLTIP_REFRESH_SECONDS)
    {
      m_BakeTooltipEntity = entity;
      m_BakeTooltipMode = int32_t(mode);
      b_BakeTooltipHidden = hidden;
      m_BakeTooltipGeneration = scene.GetStructureGeneration();
      m_BakeTooltipTime = now;
      m_BakeTooltip = "Auto leaves out anything with a dynamic collider on itself or an ancestor. Include and "
        "Exclude override that for this entity and everything below it; the nearest override up the hierarchy wins.\n\n"
        "Effective: " + DescribeBakeInclusion(scene, entity) + ".";
    }

    if (PropertyEnum("Mode", mode, BAKE_MODES, {
      .defaultValue = int32_t(BakeOverride::Auto),
      .tooltip = m_BakeTooltip.c_str(),
      .disabledReason = scatterInstance
        ? "Runtime scatter output, regenerated by its scatter parent. Set the override on the scatter parent; it covers "
          "this whole subtree."
        : nullptr }).changed)
    {
      if (mode == BakeOverride::Auto)
        registry.remove<BakeOverrideComponent>(entity);
      else
        registry.emplace_or_replace<BakeOverrideComponent>(entity, BakeOverrideComponent { .mode = mode });
    }

    EndPropertyGroup();
  }

  void DetailsPanel::DrawTerrainSection(EditorContext& context, Entity entity)
  {
    Scene& scene = *context.scene;
    bool remove = false;
    if (BeginSection("Terrain", ICON_LC_MOUNTAIN, "Generated heightfield mesh", &remove))
    {
      DrawTerrainContents(context, entity, scene.GetComponent<TerrainComponent>(entity));
      EndPropertyGroup();
    }

    if (!remove)
      return;

    scene.RemoveComponent<TerrainComponent>(entity);
    if (scene.HasComponent<TerrainDirty>(entity))
      scene.RemoveComponent<TerrainDirty>(entity);
    if (scene.HasComponent<TerrainMaterialComponent>(entity))
      scene.RemoveComponent<TerrainMaterialComponent>(entity);
    DestroyGeneratedMesh(context, entity);
  }

  void DetailsPanel::DrawTerrainMaterialSection(EditorContext& context, Entity entity)
  {
    Scene& scene = *context.scene;
    bool remove = false;
    if (BeginSection("Terrain Material", ICON_LC_LAYERS, "Slope and shoulder layers blended over the terrain", &remove))
    {
      DrawTerrainMaterialContents(scene.GetComponent<TerrainMaterialComponent>(entity));
      EndPropertyGroup();
    }

    if (remove)
      scene.RemoveComponent<TerrainMaterialComponent>(entity);
  }

  void DetailsPanel::DrawRoadSection(EditorContext& context, Entity entity)
  {
    Scene& scene = *context.scene;
    bool remove = false;
    if (BeginSection("Road", ICON_LC_ROUTE, "Generated road mesh along a spline", &remove))
    {
      RoadComponent& road = scene.GetComponent<RoadComponent>(entity);
      PropertyEdit edit;

      edit |= PropertyFloat("Width", road.width, {
        .min = 0.1f, .max = 100.0f, .speed = 0.1f, .format = "%.2f", .unit = "m", .defaultValue = 4.0f,
        .tooltip = "Full width of the road surface." });
      edit |= PropertyFloat("UV Scale", road.uvScale, {
        .min = 0.01f, .max = 100.0f, .speed = 0.01f, .format = "%.2f", .defaultValue = 1.0f,
        .tooltip = "Texture repeats per meter along the road." });
      edit |= PropertyUInt("Segments", road.segments, {
        .min = 2, .max = 512, .speed = 1.0f, .defaultValue = 64,
        .tooltip = "Quads along the whole road; more follow its curves more closely." });

      PropertySubHeading("Control Points");
      const bool pointsCommitted = DrawRoadPoints(entity, road);

      PropertySubHeading("Terrain Carving");
      edit |= PropertyFloat("Carve Outer Radius", road.carveOuterRadius, {
        .min = 0.0f, .max = 50.0f, .speed = 0.1f, .format = "%.2f", .unit = "m", .defaultValue = 0.0f,
        .tooltip = "Distance from the road's center line out to which terrains are pulled toward the road's height. 0 "
                   "switches carving off." });

      PushDependency(road.carveOuterRadius > 0.0f, "Requires Carve Outer Radius above 0");
      edit |= PropertyFloat("Carve Inner Radius", road.carveInnerRadius, {
        .min = 0.0f, .max = 50.0f, .speed = 0.1f, .format = "%.2f", .unit = "m", .defaultValue = 0.0f,
        .tooltip = "Distance from the road's center line within which terrains sit fully at the road's height." });
      edit |= PropertyFloat("Carve Depth Offset", road.carveDepthOffset, {
        .min = 0.0f, .max = 5.0f, .speed = 0.01f, .format = "%.2f", .unit = "m", .defaultValue = 0.0f,
        .tooltip = "How far below the road surface the carved terrain sits, so it does not poke through the road." });

      if (road.carveCurve.empty())
      {
        if (PropertyButton("Add Carve Curve", {
          .icon = ICON_LC_CHART_SPLINE,
          .tooltip = "Shapes the blend from Carve Inner Radius out to Carve Outer Radius, which is a smoothstep without "
                     "a curve." }))
        {
          road.carveCurve = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
          edit.committed = true;
        }
      }
      else
      {
        if (BeginPropertyRow("Carve Curve", { .tooltip = CARVE_CURVE_TOOLTIP.c_str() }))
        {
          const CurveEdit curve = CurveEditor::Edit("Canvas", road.carveCurve);
          edit.changed |= curve.changed;
          edit.committed |= curve.committed;
          EndPropertyRow();
        }
        if (PropertyButton("Remove Carve Curve", {
          .icon = ICON_LC_TRASH_2, .tooltip = "Goes back to the smoothstep blend" }))
        {
          road.carveCurve.clear();
          edit.committed = true;
        }
      }
      PopDependency();

      // Terrains regenerate from dirty roads as well, which is what applies the carving
      if (edit.committed || pointsCommitted)
        scene.GetRegistry().emplace_or_replace<RoadDirty>(entity);

      EndPropertyGroup();
    }

    if (!remove)
      return;

    scene.RemoveComponent<RoadComponent>(entity);
    if (scene.HasComponent<RoadDirty>(entity))
      scene.RemoveComponent<RoadDirty>(entity);
    DestroyGeneratedMesh(context, entity);
  }

  bool DetailsPanel::DrawRoadPoints(Entity entity, RoadComponent& road)
  {
    if (!b_RoadViewLocked || m_RoadViewEntity != entity)
    {
      glm::vec2 minXZ(std::numeric_limits<float>::max());
      glm::vec2 maxXZ(std::numeric_limits<float>::lowest());
      for (const glm::vec3& point : road.points)
      {
        minXZ = glm::min(minXZ, glm::vec2(point.x, point.z));
        maxXZ = glm::max(maxXZ, glm::vec2(point.x, point.z));
      }

      const glm::vec2 extent = road.points.empty() ? glm::vec2(0.0f) : maxXZ - minXZ;
      float range = std::max(extent.x, extent.y);
      if (range < MIN_ROAD_VIEW_RANGE)
        range = DEFAULT_ROAD_VIEW_RANGE;
      range /= 1.0f - 2.0f * ROAD_VIEW_MARGIN;

      const glm::vec2 center = road.points.empty() ? glm::vec2(0.0f) : (minXZ + maxXZ) * 0.5f;
      m_RoadViewEntity = entity;
      m_RoadViewOrigin = center - glm::vec2(range * 0.5f);
      m_RoadViewRange = range;
    }

    // The canvas has Y up; flipping Z puts +Z at the bottom, as seen from above
    std::vector<glm::vec2>& canvasPoints = m_RoadCanvasPoints;
    canvasPoints.clear();
    for (const glm::vec3& point : road.points)
    {
      const glm::vec2 normalized = (glm::vec2(point.x, point.z) - m_RoadViewOrigin) / m_RoadViewRange;
      canvasPoints.push_back(glm::vec2(normalized.x, 1.0f - normalized.y));
    }

    std::vector<glm::vec2>& unedited = m_RoadUneditedPoints;
    unedited.assign(canvasPoints.begin(), canvasPoints.end());
    SplinePathEdit path;
    if (BeginPropertyRow("Path", { .tooltip = ROAD_PATH_TOOLTIP.c_str() }))
    {
      path = SplinePathEditor::Edit("Canvas", canvasPoints);
      EndPropertyRow();
    }

    // The canvas works on a copy without heights, so its insert and remove are replayed on the real
    // points by index
    if (path.insertedIndex >= 0 && size_t(path.insertedIndex) <= road.points.size())
    {
      const size_t index = size_t(path.insertedIndex);
      const float height = InsertedPointHeight(road.points, index);
      road.points.insert(road.points.begin() + index, glm::vec3(0.0f, height, 0.0f));
      // No canvas position maps onto this one, so the new point is always written below
      unedited.insert(unedited.begin() + path.insertedIndex, glm::vec2(-1.0f));
    }
    else if (path.removedIndex >= 0 && size_t(path.removedIndex) < road.points.size())
    {
      road.points.erase(road.points.begin() + path.removedIndex);
      unedited.erase(unedited.begin() + path.removedIndex);
    }

    // Written on every drag step, not only on release: the copy is rebuilt from the points each frame.
    // Only points the canvas moved are written, so the others do not pick up round-off from the mapping.
    if (path.changed && canvasPoints.size() == road.points.size() && unedited.size() == road.points.size())
    {
      for (size_t i = 0; i < road.points.size(); i++)
      {
        if (canvasPoints[i] == unedited[i])
          continue;

        const glm::vec2 world = glm::vec2(canvasPoints[i].x, 1.0f - canvasPoints[i].y) * m_RoadViewRange + m_RoadViewOrigin;
        road.points[i].x = world.x;
        road.points[i].z = world.y;
      }
    }
    b_RoadViewLocked = path.active;

    PropertyEdit heights;
    for (size_t i = 0; i < road.points.size(); i++)
    {
      char label[32];
      std::snprintf(label, sizeof(label), "Point %zu Height", i);
      heights |= PropertyFloat(label, road.points[i].y, {
        .speed = 0.05f, .format = "%.2f", .unit = "m",
        .tooltip = "Height of this control point in the road's local space; the road passes through every point." });
    }

    return path.committed || heights.committed;
  }

  void DetailsPanel::DrawScatterSection(EditorContext& context, Entity entity)
  {
    Scene& scene = *context.scene;
    bool remove = false;
    if (BeginSection("Scatter", ICON_LC_SPROUT, "Instances scattered over the terrain", &remove))
    {
      DrawScatterContents(context, entity, scene.GetComponent<ScatterComponent>(entity), m_ClusterSourceLookup);
      EndPropertyGroup();
    }

    if (!remove)
      return;

    scene.RemoveComponent<ScatterComponent>(entity);
    if (scene.HasComponent<ScatterDirty>(entity))
      scene.RemoveComponent<ScatterDirty>(entity);
  }

  void DetailsPanel::DrawColliderSection(EditorContext& context, Entity entity)
  {
    Scene& scene = *context.scene;
    const ModelSourceComponent* model = scene.GetRegistry().try_get<ModelSourceComponent>(entity);
    const char* built = model != nullptr && model->colliderEnabled
      ? "Built by the Model section's collider, which rebuilds it from the model and overwrites edits made here"
      : nullptr;

    bool remove = false;
    if (BeginSection("Collider", ICON_LC_BOX, "Box collider for physics and collision queries", &remove, built))
    {
      ColliderComponent& collider = scene.GetComponent<ColliderComponent>(entity);
      DrawColliderProperties(ColliderFields {
        .offset = &collider.localOffset, .halfExtents = &collider.halfExtents,
        .isStatic = &collider.isStatic, .layer = &collider.layer, .mask = &collider.mask }, built);
      EndPropertyGroup();
    }

    if (remove)
      scene.RemoveComponent<ColliderComponent>(entity);
  }

  void DetailsPanel::DrawAddComponent(EditorContext& context, Entity entity)
  {
    ImGui::Spacing();
    if (InlineButton("Add Component", {
      .icon = ICON_LC_CIRCLE_PLUS, .width = -1.0f,
      .tooltip = "Lists the components this entity can take; the Outliner's context menu offers the same list" }))
    {
      ImGui::OpenPopup("AddComponentPopup");
    }

    if (!ImGui::BeginPopup("AddComponentPopup"))
      return;

    AddComponentMenuItems(context, entity);
    ImGui::EndPopup();
  }

  void DetailsPanel::OnRender(EditorContext& context)
  {
    if (!BeginPanel())
    {
      ImGui::End();
      return;
    }

    Scene* scene = context.scene;
    Entity entity = context.selectedEntity;
    if (scene == nullptr || entity == entt::null || !scene->GetRegistry().valid(entity))
    {
      ImGui::TextDisabled("No entity selected");
      ImGui::End();
      return;
    }

    DrawHeader(context, entity);
    ImGui::Spacing();

    // Presence is checked right before each section: a Remove earlier in this frame takes its component away
    for (const Section& section : SECTIONS)
    {
      if (section.present(*scene, entity))
        (this->*section.draw)(context, entity);
    }

    DrawAddComponent(context, entity);
    ImGui::End();
  }
}
