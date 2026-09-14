#include "Editor/Panels/DetailsPanel.h"

#include <imgui.h>

#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorIcons.h"
#include "Editor/Utils/FileDialog.h"
#include "Editor/Utils/CurveEditor.h"
#include "Editor/Utils/SplinePathEditor.h"
#include "Scene/Scene.h"
#include "Scene/BakeExclusion.h"
#include "Scene/Components.h"
#include "Scene/ModelOverrides.h"
#include "Assets/AssetManager.h"
#include "Render/Render.h"
#include "Render/BakeLimits.h"
#include "Utils/IrradianceGrid.h"

namespace YAEngine
{
  static bool ColoredDragFloat3(const char* label, float* values, float speed,
    float resetValue, float min, float max)
  {
    bool changed = false;

    ImGui::PushID(label);

    float lineHeight = ImGui::GetFrameHeight();
    ImVec2 buttonSize = { lineHeight, lineHeight };

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.1f, 0.15f, 1.0f));
    if (ImGui::Button("X", buttonSize))
    {
      values[0] = resetValue;
      changed = true;
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::CalcItemWidth() / 3.0f - buttonSize.x);
    if (ImGui::DragFloat("##X", &values[0], speed, min, max, "%.2f"))
      changed = true;

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
    if (ImGui::Button("Y", buttonSize))
    {
      values[1] = resetValue;
      changed = true;
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::CalcItemWidth() / 3.0f - buttonSize.x);
    if (ImGui::DragFloat("##Y", &values[1], speed, min, max, "%.2f"))
      changed = true;

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.25f, 0.8f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.35f, 0.9f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.25f, 0.8f, 1.0f));
    if (ImGui::Button("Z", buttonSize))
    {
      values[2] = resetValue;
      changed = true;
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::CalcItemWidth() / 3.0f - buttonSize.x);
    if (ImGui::DragFloat("##Z", &values[2], speed, min, max, "%.2f"))
      changed = true;

    ImGui::SameLine();
    ImGui::Text("%s", label);

    ImGui::PopID();
    return changed;
  }

  static void DrawTransform(EditorContext& context, LocalTransform& lt)
  {
    if (ImGui::CollapsingHeader(ICON_FA_UP_DOWN_LEFT_RIGHT " Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
      bool dirty = false;

      if (ColoredDragFloat3("Position", &lt.position.x, 0.1f, 0.0f, 0.0f, 0.0f))
        dirty = true;

      glm::vec3 euler = glm::degrees(glm::eulerAngles(lt.rotation));
      if (ColoredDragFloat3("Rotation", &euler.x, 0.5f, 0.0f, 0.0f, 0.0f))
      {
        lt.rotation = glm::quat(glm::radians(euler));
        dirty = true;
      }

      if (ColoredDragFloat3("Scale", &lt.scale.x, 0.1f, 1.0f, 0.001f, 1000.0f))
        dirty = true;

      if (context.scene->HasComponent<LocalBounds>(context.selectedEntity))
      {
        auto& bounds = context.scene->GetComponent<LocalBounds>(context.selectedEntity);
        ImGui::Text("MinBB: (%.1f, %.1f, %.1f)", bounds.min.x, bounds.min.y, bounds.min.z);
        ImGui::Text("MaxBB: (%.1f, %.1f, %.1f)", bounds.max.x, bounds.max.y, bounds.max.z);
      }

      if (dirty)
        context.scene->MarkDirty(context.selectedEntity);
    }
  }

  static void DrawMesh(EditorContext& context, MeshComponent& mc)
  {
    if (ImGui::CollapsingHeader(ICON_FA_DRAW_POLYGON " Mesh", ImGuiTreeNodeFlags_DefaultOpen))
    {
      auto entity = context.selectedEntity;
      auto& scene = *context.scene;

      bool visible = !scene.HasComponent<HiddenTag>(entity);
      if (ImGui::Checkbox("Render", &visible))
      {
        if (!visible) scene.AddComponent<HiddenTag>(entity);
        else scene.RemoveComponent<HiddenTag>(entity);
      }

      auto* instanceData = context.assetManager->Meshes().GetInstanceData(mc.asset);
      if (instanceData != nullptr)
      {
        ImGui::Text("Instance count: %zu", instanceData->size());
        ImGui::Text("Offset: %u", context.assetManager->Meshes().GetInstanceOffset(mc.asset));
      }
    }
  }

  static void DrawMaterial(EditorContext& context, MaterialComponent& mc)
  {
    if (ImGui::CollapsingHeader(ICON_FA_PALETTE " Material", ImGuiTreeNodeFlags_DefaultOpen))
    {
      auto& materials = context.assetManager->Materials();

      const char* currentName = "None";
      if (auto* matPtr = materials.TryGet(mc.asset))
      {
        auto& mat = *matPtr;
        currentName = mat.name.c_str();
        ImGui::ColorButton("##matcolor", ImVec4(mat.albedo.x, mat.albedo.y, mat.albedo.z, 1.0f),
          ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(20, 20));
        ImGui::SameLine();
      }

      ImGui::SetNextItemWidth(-1);
      if (ImGui::BeginCombo("##material", currentName))
      {
        materials.ForEachWithHandle([&](MaterialHandle handle, Material& mat)
        {
          ImGui::PushID(static_cast<int>(handle.index));
          bool isSelected = (mc.asset == handle);
          if (ImGui::Selectable(mat.name.c_str(), isSelected))
            mc.asset = handle;

          if (isSelected)
            ImGui::SetItemDefaultFocus();
          ImGui::PopID();
        });

        ImGui::EndCombo();
      }

      if (auto* matPtr2 = materials.TryGet(mc.asset))
      {
        auto& mat = *matPtr2;

        if (ImGui::Checkbox("Double Sided", &mat.doubleSided))
          mat.MarkChanged();

        bool unlit = (mat.shadingModel == ShadingModel::Unlit);
        if (ImGui::Checkbox("Unlit", &unlit))
        {
          mat.shadingModel = unlit ? ShadingModel::Unlit : ShadingModel::Lit;
          mat.MarkChanged();
        }

        if (ImGui::Checkbox("Emissive", &mat.emissive))
          mat.MarkChanged();
      }

    }
  }

  static bool DrawLight(EditorContext& context, LightComponent& light)
  {
    bool open = ImGui::CollapsingHeader(ICON_FA_LIGHTBULB " Light", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
    if (ImGui::Button(ICON_FA_XMARK "##RemoveLight", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
    {
      ImGui::PopStyleColor(3);
      return true;
    }
    ImGui::PopStyleColor(3);

    if (open)
    {
      const char* lightTypes[] = { "Point", "Spot", "Directional" };
      int currentType = static_cast<int>(light.type);
      if (ImGui::Combo("Type", &currentType, lightTypes, IM_ARRAYSIZE(lightTypes)))
        light.type = static_cast<LightType>(currentType);

      ImGui::ColorEdit3("Color", &light.color.x);
      ImGui::DragFloat("Intensity", &light.intensity, 0.1f, 0.0f, FLT_MAX);

      if (light.type == LightType::Point || light.type == LightType::Spot)
        ImGui::DragFloat("Radius", &light.radius, 0.5f, 0.0f, FLT_MAX);

      if (light.type == LightType::Spot)
      {
        ImGui::SliderAngle("Inner Cone", &light.innerCone, 0.0f, 90.0f);
        ImGui::SliderAngle("Outer Cone", &light.outerCone, 0.0f, 90.0f);
      }

      ImGui::Checkbox("Cast Shadow", &light.castShadow);

      if (light.type == LightType::Directional && light.castShadow)
        ImGui::DragFloat("Shadow Distance", &light.shadowDistance, 1.0f, 1.0f, 5000.0f, "%.1f");
    }

    return false;
  }

  static bool DrawReflectionProbe(EditorContext& context, ReflectionProbeComponent& lp)
  {
    ImGui::PushID("ReflectionProbe");
    bool open = ImGui::CollapsingHeader(ICON_FA_GLOBE " Reflection Probe", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
    if (ImGui::Button(ICON_FA_XMARK "##RemoveProbe", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
    {
      ImGui::PopStyleColor(3);
      ImGui::PopID();
      return true;
    }
    ImGui::PopStyleColor(3);

    if (open)
    {
      const char* shapeNames[] = { "Sphere", "Box" };
      int currentShape = static_cast<int>(lp.shape);
      if (ImGui::Combo("Shape", &currentShape, shapeNames, IM_ARRAYSIZE(shapeNames)))
        lp.shape = static_cast<ProbeShape>(currentShape);

      if (lp.shape == ProbeShape::Sphere)
      {
        ImGui::DragFloat("Radius", &lp.extents.x, 0.1f, 0.1f, 1000.0f);
        lp.extents.y = lp.extents.x;
        lp.extents.z = lp.extents.x;
      }
      else
      {
        ImGui::DragFloat3("Extents", &lp.extents.x, 0.1f, 0.1f, 1000.0f);
        ImGui::TextDisabled("Rotation comes from the entity transform (rotate gizmo, key 2).");
        ImGui::TextDisabled("Transform scale is ignored - extents define the volume.");
      }

      ImGui::DragFloat("Fade Distance", &lp.fadeDistance, 0.1f, 0.0f, 100.0f);
      ImGui::DragInt("Priority", &lp.priority, 1, -100, 100);

      ImGui::Checkbox("Parallax Correction", &lp.parallaxCorrection);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reproject reflections onto the proxy volume.\nOnly correct while the volume matches the real geometry.");

      if (lp.parallaxCorrection)
      {
        // Seeded from the influence volume so switching the override on is a no-op
        // until the values are actually moved - otherwise the reflection jumps.
        if (ImGui::Checkbox("Custom Proxy Volume", &lp.customProxyVolume) && lp.customProxyVolume)
        {
          lp.proxyOffset = glm::vec3(0.0f);
          lp.proxyExtents = lp.extents;
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Give parallax its own volume, separate from the influence volume.\nLets a thin influence slab reproject onto a deep proxy box.");

        if (lp.customProxyVolume)
        {
          ImGui::Indent();
          ImGui::DragFloat3("Proxy Offset", &lp.proxyOffset.x, 0.1f, -1000.0f, 1000.0f);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Proxy centre relative to the probe, in probe local space.");

          if (lp.shape == ProbeShape::Sphere)
          {
            ImGui::DragFloat("Proxy Radius", &lp.proxyExtents.x, 0.1f, 0.1f, 1000.0f);
            lp.proxyExtents.y = lp.proxyExtents.x;
            lp.proxyExtents.z = lp.proxyExtents.x;
          }
          else
          {
            ImGui::DragFloat3("Proxy Extents", &lp.proxyExtents.x, 0.1f, 0.1f, 1000.0f);
          }
          ImGui::TextDisabled("Drawn in green by the probe volume gizmo.");
          ImGui::Unindent();
        }
      }

      // Options come from the baker range, so the combo can never offer a value
      // the bake path would clamp away.
      if (ImGui::BeginCombo("Resolution", std::to_string(lp.resolution).c_str()))
      {
        for (uint32_t option = BakeLimits::PROBE_MIN_CAPTURE_RESOLUTION;
             option <= BakeLimits::PROBE_MAX_CAPTURE_RESOLUTION; option *= 2)
        {
          bool isSelected = (lp.resolution == option);
          if (ImGui::Selectable(std::to_string(option).c_str(), isSelected))
            lp.resolution = option;

          if (isSelected)
            ImGui::SetItemDefaultFocus();
        }

        ImGui::EndCombo();
      }

      if (lp.baked)
      {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "Baked (slot %u)", lp.atlasSlot);
        if (!lp.bakedPrefilterPath.empty())
          ImGui::TextDisabled("%s", lp.bakedPrefilterPath.c_str());

        static bool s_ProbePreviewOpen = false;
        static uint32_t s_ProbePreviewSlot = 0;

        if (ImGui::Button(ICON_FA_EYE " Preview"))
        {
          s_ProbePreviewOpen = true;
          s_ProbePreviewSlot = lp.atlasSlot;
        }

        if (s_ProbePreviewOpen && s_ProbePreviewSlot == lp.atlasSlot)
        {
          ImGui::SetNextWindowSize(ImVec2(440, 380), ImGuiCond_FirstUseEver);
          if (ImGui::Begin("Reflection Probe Preview", &s_ProbePreviewOpen))
          {
            auto& atlas = context.render->GetProbeAtlas();
            auto& ctx = context.render->GetContext();
            const char* faceLabels[] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
            float imageSize = 128.0f;

            ImGui::SeparatorText("Prefilter");
            for (uint32_t row = 0; row < 2; row++)
            {
              for (uint32_t col = 0; col < 3; col++)
              {
                uint32_t face = row * 3 + col;
                VkDescriptorSet ds = atlas.GetPrefilterFacePreview(ctx, lp.atlasSlot, face);
                if (col > 0) ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::Image((ImTextureID)ds, ImVec2(imageSize, imageSize));
                ImGui::TextDisabled("%s", faceLabels[face]);
                ImGui::EndGroup();
              }
            }
          }
          ImGui::End();
        }
      }
      else
      {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "Not baked");
      }

      if (ImGui::Button(ICON_FA_CIRCLE_PLAY " Bake", ImVec2(-1, 0)))
      {
        context.render->BakeProbe(context.selectedEntity, *context.scene, *context.assetManager);
      }
    }

    ImGui::PopID();
    return false;
  }

  static void DrawIrradianceVolumePlacement(EditorContext& context, bool bakeAvailable)
  {
    const ImVec4 warningColor(1.0f, 0.6f, 0.2f, 1.0f);
    const ImVec4 errorColor(1.0f, 0.4f, 0.4f, 1.0f);

    ImGui::Separator();
    ImGui::BeginDisabled(!bakeAvailable);
    if (ImGui::Button(ICON_FA_BORDER_ALL " Preview Placement", ImVec2(-1, 0)))
      context.render->PreviewIrradianceVolumePlacement(context.selectedEntity, *context.scene, *context.assetManager);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
      if (bakeAvailable)
        ImGui::SetTooltip("Lays out the sparse bricks the volume would bake with: they refine from\n"
          "Max Spacing down to Min Spacing where ray traced queries find geometry near.\n"
          "Nothing is baked.");
      else
        ImGui::SetTooltip("The placement preview finds geometry by ray tracing, and the ray traced baker is\n"
          "unavailable: no hardware ray tracing pipeline or no bindless texture table.");
    }

    const IrradianceVolumePlacementPreview* preview =
      context.render->FindIrradianceVolumePlacementPreview(context.selectedEntity);
    if (preview == nullptr)
      return;

    if (Render::ComputeIrradianceVolumePlacementFingerprint(*context.scene, context.selectedEntity) != preview->fingerprint)
    {
      ImGui::TextColored(warningColor, "Stale: the volume changed since the preview");
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Only this volume's own parameters are tracked: its transform, half extents,\n"
          "spacings and backface threshold. Scene geometry, bake overrides, hidden entities\n"
          "and model reloads are not - preview again after changing those.");
    }

    if (!preview->error.empty())
    {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextColored(errorColor, "Preview failed: %s", preview->error.c_str());
      ImGui::PopTextWrapPos();
      return;
    }

    if (!preview->validationPassed)
    {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextColored(errorColor, "Validation failed: %s", preview->validationFailure.c_str());
      ImGui::PopTextWrapPos();
    }

    const IrradianceVolumePlacementEstimate estimate = Render::EstimateIrradianceVolumePlacement(*preview, 0);

    if (ImGui::BeginTable("PlacementLevels", 4, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchSame))
    {
      ImGui::TableSetupColumn("Spacing");
      ImGui::TableSetupColumn("Bricks");
      ImGui::TableSetupColumn("Nodes");
      ImGui::TableSetupColumn("Stitched");
      ImGui::TableHeadersRow();

      for (uint32_t level = preview->maxSpacingIndex + 1; level-- > preview->minSpacingIndex;)
      {
        const IrradianceBrickLevelStats& stats = preview->levelStats[level];
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
      ImGui::Text("%u", estimate.bricks);
      ImGui::TableNextColumn();
      ImGui::Text("%u", estimate.uniqueNodes);
      ImGui::TableNextColumn();
      ImGui::Text("%u", estimate.stitchedNodes);
      ImGui::EndTable();
    }

    constexpr double BYTES_PER_MB = 1024.0 * 1024.0;
    ImGui::Text("VRAM ~%.1f MB, disk ~%.1f MB", double(estimate.runtimeBytes) / BYTES_PER_MB,
      double(estimate.diskBytes) / BYTES_PER_MB);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Estimated for the planned brick format. VRAM: 25 bytes per brick texel\n"
        "(three RGBA16F coefficient textures, one R8 validity) plus 4 per indirection cell.\n"
        "Disk: 25 bytes per unique node, node indices per brick, 4 per indirection cell.\n"
        "%u indirection cells.", estimate.indirectionCells);
    ImGui::TextDisabled("Layout %.2f s (GPU queries %.2f s, %u points), total %.2f s",
      preview->layoutSeconds, preview->querySeconds, preview->queryPoints, preview->totalSeconds);

    ImGui::Checkbox("Show Bricks", &context.render->GetVolumeBricksVisible());
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Wireframes of the previewed bricks while this volume is selected,\n"
        "colored by spacing as in the table above.");
    // With gizmos off no brick is drawn, so a drawn share would be wrong.
    if (!context.render->GetVolumeBricksVisible() || !context.render->GetGizmosEnabled())
      return;

    const auto strides = Render::GetPreviewBrickDrawStrides(*preview, context.render->GetVolumeNodeGizmosDrawn());
    uint32_t drawnBricks = 0;
    for (uint32_t level = 0; level < uint32_t(strides.size()); level++)
      drawnBricks += (preview->levelStats[level].bricks + strides[level] - 1) / strides[level];
    if (drawnBricks < estimate.bricks)
    {
      ImGui::SameLine();
      ImGui::TextColored(warningColor, "%u of %u drawn", drawnBricks, estimate.bricks);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Every k-th brick of each spacing level, so every level stays visible.\n"
          "At most %u bricks, fewer by the nodes Volume Nodes draws, down to %u.",
          Render::MAX_DRAWN_PREVIEW_BRICKS, Render::MAX_DRAWN_PREVIEW_BRICKS_WITH_NODES);
    }
  }

  // The bake lays its bricks out exactly as the placement preview does, so the kept preview is
  // what predicts its cost.
  static void DrawIrradianceVolumeBakeEstimate(EditorContext& context)
  {
    const ImVec4 warningColor(1.0f, 0.6f, 0.2f, 1.0f);

    const IrradianceVolumePlacementPreview* preview =
      context.render->FindIrradianceVolumePlacementPreview(context.selectedEntity);
    if (preview == nullptr || !preview->error.empty())
    {
      ImGui::TextDisabled("Bake: Preview Placement shows the node count");
      return;
    }

    const uint32_t volumeSamples = uint32_t(std::clamp(context.render->GetVolumeSampleCount(),
      Render::MIN_VOLUME_SAMPLES, Render::MAX_VOLUME_SAMPLES));
    const IrradianceVolumePlacementEstimate estimate = Render::EstimateIrradianceVolumePlacement(*preview, volumeSamples);
    const bool stale = Render::ComputeIrradianceVolumePlacementFingerprint(*context.scene, context.selectedEntity)
      != preview->fingerprint;
    const bool manySamples = estimate.primarySamples > BakeLimits::VOLUME_WARN_PRIMARY_SAMPLES;

    ImGui::TextColored(manySamples ? warningColor : ImGui::GetStyleColorVec4(ImGuiCol_Text),
      "Bake: %u nodes x %u = %llu primary samples%s", estimate.bakedNodes, volumeSamples,
      (unsigned long long)estimate.primarySamples, stale ? " (stale preview)" : "");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("From the placement preview below. Unique nodes that are not stitched, times\n"
        "Volume Samples (Render Settings), each tracing paths of up to Volume Bounces bounces.\n"
        "Buried nodes are traced like the rest and only rejected afterwards, those near a back\n"
        "face once more from their virtual offset; stitched nodes are interpolated from their\n"
        "coarser neighbour instead of baked.");
    if (manySamples)
      ImGui::TextColored(warningColor, "Warning: many primary samples, the bake will take a while");
  }

  static bool DrawIrradianceVolume(EditorContext& context, IrradianceVolumeComponent& iv)
  {
    ImGui::PushID("IrradianceVolume");
    bool open = ImGui::CollapsingHeader(ICON_FA_CUBES " Irradiance Volume", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
    if (ImGui::Button(ICON_FA_XMARK "##RemoveVolume", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
    {
      ImGui::PopStyleColor(3);
      ImGui::PopID();
      return true;
    }
    ImGui::PopStyleColor(3);

    if (open)
    {
      // AlwaysClamp: without it Ctrl+Click text entry ignores the range, and the
      // node count is derived from these.
      ImGui::DragFloat3("Half Extents", &iv.halfExtents.x, 0.1f, 0.1f, 1000.0f,
        "%.3f", ImGuiSliderFlags_AlwaysClamp);
      ImGui::TextDisabled("Position and rotation come from the entity transform.");
      ImGui::TextDisabled("Transform scale is ignored - half extents define the box.");

      // Only powers of two, and only from the shared set - that is what makes the
      // nodes of a coarse volume a subset of a finer one on the world lattice.
      const char* spacingOptions[] = { "0.25", "0.5", "1", "2", "4" };
      static_assert(IM_ARRAYSIZE(spacingOptions) == IRRADIANCE_SPACINGS.size(),
        "Spacing combo labels must match IRRADIANCE_SPACINGS");
      // A value from a hand-edited scene or from code is pulled onto the set here,
      // so what the combos show is also what gets saved.
      iv.minSpacing = SnapIrradianceSpacing(iv.minSpacing);
      iv.maxSpacing = std::max(SnapIrradianceSpacing(iv.maxSpacing), iv.minSpacing);
      int minSpacingIdx = 0;
      int maxSpacingIdx = 0;
      for (int i = 0; i < IM_ARRAYSIZE(spacingOptions); i++)
      {
        if (IRRADIANCE_SPACINGS[i] == iv.minSpacing) minSpacingIdx = i;
        if (IRRADIANCE_SPACINGS[i] == iv.maxSpacing) maxSpacingIdx = i;
      }
      if (ImGui::Combo("Min Spacing", &minSpacingIdx, spacingOptions, IM_ARRAYSIZE(spacingOptions)))
      {
        iv.minSpacing = IRRADIANCE_SPACINGS[minSpacingIdx];
        iv.maxSpacing = std::max(iv.maxSpacing, iv.minSpacing);
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Finest node spacing in meters, reached next to geometry.\n"
          "Nodes sit on one world lattice shared by every volume, so the box is\n"
          "snapped to the lattice instead. Overlapping volumes then agree.");
      if (ImGui::Combo("Max Spacing", &maxSpacingIdx, spacingOptions, IM_ARRAYSIZE(spacingOptions)))
      {
        iv.maxSpacing = IRRADIANCE_SPACINGS[maxSpacingIdx];
        iv.minSpacing = std::min(iv.minSpacing, iv.maxSpacing);
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Coarsest node spacing in meters, kept in open air.");

      ImGui::DragFloat("Backface Threshold", &iv.backfaceRatioThreshold, 0.01f,
        BakeLimits::VOLUME_MIN_BACKFACE_THRESHOLD, BakeLimits::VOLUME_MAX_BACKFACE_THRESHOLD,
        "%.2f", ImGuiSliderFlags_AlwaysClamp);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fraction of a node's probe rays that may hit the inside of single-sided\n"
          "geometry before the bake rejects the node as buried behind a wall or under the ground.\n"
          "Rejected nodes take the average of their nearest valid neighbours.\n"
          "Lower catches more leaks, higher keeps more nodes. 1.00 keeps every node.");

      ImGui::Checkbox("Virtual Offset", &iv.virtualOffset);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Buried nodes within their spacing of a back face are moved past that face\n"
          "and integrated again instead of taking their neighbours' light. An offset node that\n"
          "is still buried, too close to geometry or enclosed is dilated as before.\n"
          "Integrates those nodes a second time. Applies on the next bake.");

      ImGui::BeginDisabled(!iv.virtualOffset);
      ImGui::DragFloat("Virtual Offset Bias", &iv.virtualOffsetBias, 0.001f,
        BakeLimits::VOLUME_MIN_VIRTUAL_OFFSET_BIAS, BakeLimits::VOLUME_MAX_VIRTUAL_OFFSET_BIAS,
        "%.3f m", ImGuiSliderFlags_AlwaysClamp);
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Extra clearance in meters beyond the minimum probe clearance, %.2f of the node's\n"
          "spacing, at which a virtual offset node is placed in front of its nearest back face.\n"
          "The whole offset, face distance plus clearance plus bias, stays within the node's\n"
          "spacing: a node that would need more is dilated instead. Applies on the next bake.",
          double(BakeLimits::VOLUME_MIN_PROBE_CLEARANCE_FRACTION));

      ImGui::DragFloat("Edge Fade", &iv.edgeFade, 0.01f, BakeLimits::VOLUME_MIN_EDGE_FADE,
        BakeLimits::VOLUME_MAX_EDGE_FADE, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Width in meters over which the volume blends into the volume enclosing it,\n"
          "or into the sky, at its box faces. Wide suits faces in open air and seams between\n"
          "nested volumes; narrow keeps the outside from reaching into a box fitted to walls.\n"
          "Stored in the baked file: a change applies on the next bake.");

      ImGui::Separator();
      DrawIrradianceVolumeBakeEstimate(context);

      // Must be the rotation the baker extracts, or the hint disagrees with the bake.
      const glm::quat rotation = ExtractIrradianceBoxRotation(context.scene->GetWorldTransform(context.selectedEntity).world);
      if (std::abs(rotation.w) < 0.9999f)
        ImGui::TextDisabled("Box is rotated - bricks stay world axis aligned, so covering\nit takes more of them than an unrotated box would need.");

      if (iv.baked)
      {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "Baked");
        if (!iv.bakedVolumePath.empty())
          ImGui::TextDisabled("%s", iv.bakedVolumePath.c_str());
      }
      else
      {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "Not baked");
      }

      const bool bakeAvailable = context.render->IsRayTracedBakeAvailable();
      ImGui::BeginDisabled(!bakeAvailable);
      if (ImGui::Button(ICON_FA_CIRCLE_PLAY " Bake", ImVec2(-1, 0)))
        context.render->BakeIrradianceVolume(context.selectedEntity, *context.scene, *context.assetManager);
      ImGui::EndDisabled();
      if (!bakeAvailable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Irradiance volumes bake by ray tracing, and the ray traced baker is\n"
          "unavailable: no hardware ray tracing pipeline or no bindless texture table.");

      DrawIrradianceVolumePlacement(context, bakeAvailable);
    }

    ImGui::PopID();
    return false;
  }

  static bool DrawRoad(EditorContext& context, RoadComponent& road)
  {
    ImGui::PushID("Road");
    bool open = ImGui::CollapsingHeader(ICON_FA_ROAD " Road", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
    if (ImGui::Button(ICON_FA_XMARK "##RemoveRoad", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
    {
      ImGui::PopStyleColor(3);
      ImGui::PopID();
      return true;
    }
    ImGui::PopStyleColor(3);

    if (open)
    {
      auto entity = context.selectedEntity;
      auto& scene = *context.scene;
      bool committed = false;

      ImGui::DragFloat("Width", &road.width, 0.1f, 0.1f, 100.0f);
      committed |= ImGui::IsItemDeactivatedAfterEdit();

      ImGui::DragFloat("UV Scale", &road.uvScale, 0.01f, 0.01f, 100.0f);
      committed |= ImGui::IsItemDeactivatedAfterEdit();

      int segs = static_cast<int>(road.segments);
      ImGui::DragInt("Segments", &segs, 1.0f, 2, 512);
      road.segments = static_cast<uint32_t>(segs);
      committed |= ImGui::IsItemDeactivatedAfterEdit();

      ImGui::Separator();
      ImGui::Text("Control Points");

      std::vector<glm::vec2> points2D;
      points2D.reserve(road.points.size());

      glm::vec2 minXZ(std::numeric_limits<float>::max());
      glm::vec2 maxXZ(-std::numeric_limits<float>::max());
      for (auto& p : road.points)
      {
        minXZ = glm::min(minXZ, glm::vec2(p.x, p.z));
        maxXZ = glm::max(maxXZ, glm::vec2(p.x, p.z));
      }
      glm::vec2 range = maxXZ - minXZ;
      float maxRange = glm::max(range.x, range.y);
      if (maxRange < 1e-4f) maxRange = 10.0f;
      glm::vec2 center = (minXZ + maxXZ) * 0.5f;
      glm::vec2 normMin = center - glm::vec2(maxRange * 0.5f);

      for (auto& p : road.points)
      {
        glm::vec2 norm = (glm::vec2(p.x, p.z) - normMin) / maxRange;
        points2D.push_back(glm::vec2(norm.x, 1.0f - norm.y));
      }

      if (SplinePathEditor::Edit("##RoadPath", points2D))
      {
        for (size_t i = 0; i < points2D.size(); i++)
        {
          glm::vec2 world = glm::vec2(points2D[i].x, 1.0f - points2D[i].y) * maxRange + normMin;
          road.points[i].x = world.x;
          road.points[i].z = world.y;
        }
        committed = true;
      }

      while (road.points.size() < points2D.size())
      {
        glm::vec2 edPt = points2D[road.points.size()];
        glm::vec2 world = glm::vec2(edPt.x, 1.0f - edPt.y) * maxRange + normMin;
        road.points.push_back(glm::vec3(world.x, 0.0f, world.y));
        committed = true;
      }
      while (road.points.size() > points2D.size())
      {
        road.points.pop_back();
        committed = true;
      }

      ImGui::Separator();
      ImGui::Text("Point Heights (Y)");
      for (size_t i = 0; i < road.points.size(); i++)
      {
        ImGui::PushID(static_cast<int>(i));
        char label[32];
        snprintf(label, sizeof(label), "Point %d Y", static_cast<int>(i));
        ImGui::DragFloat(label, &road.points[i].y, 0.1f);
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::PopID();
      }

      ImGui::Separator();
      ImGui::Text("Terrain Carving");

      ImGui::DragFloat("Carve Inner Radius", &road.carveInnerRadius, 0.1f, 0.0f, 50.0f);
      committed |= ImGui::IsItemDeactivatedAfterEdit();

      ImGui::DragFloat("Carve Outer Radius", &road.carveOuterRadius, 0.1f, 0.0f, 50.0f);
      committed |= ImGui::IsItemDeactivatedAfterEdit();

      ImGui::DragFloat("Carve Depth Offset", &road.carveDepthOffset, 0.01f, 0.0f, 5.0f);
      committed |= ImGui::IsItemDeactivatedAfterEdit();

      if (road.carveCurve.empty())
      {
        if (ImGui::Button("Add Carve Curve"))
        {
          road.carveCurve = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
          committed = true;
        }
      }
      else
      {
        ImGui::Text("Carve Curve");
        if (CurveEditor::Edit("##CarveCurve", road.carveCurve))
          committed = true;

        if (ImGui::Button("Remove Carve Curve"))
        {
          road.carveCurve.clear();
          committed = true;
        }
      }

      if (committed && !scene.GetRegistry().all_of<RoadDirty>(entity))
        scene.GetRegistry().emplace<RoadDirty>(entity);
    }

    ImGui::PopID();
    return false;
  }

  static bool DrawTerrain(EditorContext& context, TerrainComponent& terrain)
  {
    ImGui::PushID("Terrain");
    bool open = ImGui::CollapsingHeader(ICON_FA_MOUNTAIN " Terrain", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
    if (ImGui::Button(ICON_FA_XMARK "##RemoveTerrain", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
    {
      ImGui::PopStyleColor(3);
      ImGui::PopID();
      return true;
    }
    ImGui::PopStyleColor(3);

    if (open)
    {
      auto entity = context.selectedEntity;
      auto& scene = *context.scene;
      bool committed = false;

      ImGui::DragFloat("Size", &terrain.size, 1.0f, 1.0f, 10000.0f);
      committed |= ImGui::IsItemDeactivatedAfterEdit();

      int subs = static_cast<int>(terrain.subdivisions);
      ImGui::DragInt("Subdivisions", &subs, 1.0f, 2, 512);
      terrain.subdivisions = static_cast<uint32_t>(subs);
      committed |= ImGui::IsItemDeactivatedAfterEdit();

      ImGui::DragFloat("UV Scale", &terrain.uvScale, 0.1f, 0.01f, 100.0f);
      committed |= ImGui::IsItemDeactivatedAfterEdit();

      ImGui::DragFloat("Height Scale", &terrain.heightScale, 0.1f, 0.0f, 1000.0f);
      committed |= ImGui::IsItemDeactivatedAfterEdit();

      ImGui::Separator();
      ImGui::Text("Heightmap");

      bool hasHeightmap = !terrain.heightmapPath.empty();
      if (hasHeightmap)
      {
        auto relativePath = context.assetManager->MakeRelative(terrain.heightmapPath);
        ImGui::TextDisabled("%s", relativePath.c_str());
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_XMARK "##ClearHeightmap"))
        {
          terrain.heightmapPath.clear();
          committed = true;
        }
      }
      else
      {
        ImGui::TextDisabled("None");
      }

      ImGui::SameLine();
      if (ImGui::Button(ICON_FA_FOLDER_OPEN "##BrowseHeightmap"))
      {
        nfdu8filteritem_t filters[] = { { "Image", "png,jpg,tga,bmp" } };
        auto path = FileDialog::OpenFile(filters, 1);
        if (!path.empty())
        {
          terrain.heightmapPath = path;
          committed = true;
        }
      }

      if (!hasHeightmap)
      {
        ImGui::Separator();
        ImGui::Text("Procedural Noise");

        const char* noiseTypes[] = { "fBm", "Ridged", "Billowy" };
        int currentNoise = static_cast<int>(terrain.noiseType);
        if (ImGui::Combo("Noise Type", &currentNoise, noiseTypes, IM_ARRAYSIZE(noiseTypes)))
        {
          terrain.noiseType = static_cast<TerrainNoiseType>(currentNoise);
          committed = true;
        }

        ImGui::DragFloat("Frequency", &terrain.frequency, 0.001f, 0.001f, 1.0f, "%.4f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();

        int oct = static_cast<int>(terrain.octaves);
        ImGui::SliderInt("Octaves", &oct, 1, 8);
        terrain.octaves = static_cast<uint32_t>(oct);
        committed |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::DragFloat("Lacunarity", &terrain.lacunarity, 0.01f, 1.0f, 4.0f);
        committed |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::DragFloat("Persistence", &terrain.persistence, 0.01f, 0.1f, 1.0f);
        committed |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::DragInt("Seed", &terrain.seed);
        committed |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::Separator();
        ImGui::Text("Domain Warping");

        ImGui::DragFloat("Warp Strength", &terrain.warpStrength, 0.5f, 0.0f, 200.0f);
        committed |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::DragFloat("Warp Frequency", &terrain.warpFrequency, 0.001f, 0.001f, 0.1f, "%.4f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::Separator();
        ImGui::Text("Height Mask");

        if (terrain.maskPath.empty())
        {
          if (ImGui::Button("Add Path"))
          {
            terrain.maskPath = { { 0.5f, 0.0f }, { 0.5f, 1.0f } };
            committed = true;
          }
        }
        else
        {
          ImGui::Text("Path (top-down)");
          if (SplinePathEditor::Edit("##MaskPath", terrain.maskPath))
            committed = true;

          ImGui::DragFloat("Falloff Radius", &terrain.maskFalloffRadius, 0.01f, 0.01f, 1.0f);
          committed |= ImGui::IsItemDeactivatedAfterEdit();

          if (terrain.maskCurve.empty())
          {
            if (ImGui::Button("Add Falloff Curve"))
            {
              terrain.maskCurve = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
              committed = true;
            }
          }
          else
          {
            ImGui::Text("Falloff Curve");
            if (CurveEditor::Edit("##MaskCurve", terrain.maskCurve))
              committed = true;
          }

          if (ImGui::Button("Remove Path"))
          {
            terrain.maskPath.clear();
            terrain.maskCurve.clear();
            committed = true;
          }
        }
      }

      if (committed && !scene.GetRegistry().all_of<TerrainDirty>(entity))
        scene.GetRegistry().emplace<TerrainDirty>(entity);
    }

    ImGui::PopID();
    return false;
  }

  static void DrawModel(EditorContext& context, ModelSourceComponent& model)
  {
    if (ImGui::CollapsingHeader(ICON_FA_FILE_IMPORT " Model", ImGuiTreeNodeFlags_DefaultOpen))
    {
      auto relativePath = context.assetManager->MakeRelative(model.path);
      ImGui::Text("Path: %s", relativePath.c_str());

      if (ImGui::Checkbox("Combined Textures", &model.combinedTextures))
      {
        ModelOverrides::SetCombinedTextures(*context.scene, *context.assetManager,
          context.selectedEntity, model.combinedTextures);
      }

      ImGui::Separator();
      ImGui::Text("Collider");

      auto entity = context.selectedEntity;
      auto& scene = *context.scene;
      bool colliderDirty = false;

      if (ImGui::Checkbox("Enabled##ModelCollider", &model.colliderEnabled))
        colliderDirty = true;

      if (model.colliderEnabled)
      {
        ImGui::DragFloat3("Offset##ModelCollider", &model.colliderOffset.x, 0.05f);
        colliderDirty |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat3("Half Extents Scale##ModelCollider", &model.colliderHalfExtentsScale.x, 0.05f, 0.0f, 10.0f);
        colliderDirty |= ImGui::IsItemDeactivatedAfterEdit();

        if (ImGui::Checkbox("Static##ModelCollider", &model.colliderIsStatic))
          colliderDirty = true;
        ImGui::DragScalar("Layer##ModelCollider", ImGuiDataType_U32, &model.colliderLayer, 1.0f);
        colliderDirty |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragScalar("Mask##ModelCollider",  ImGuiDataType_U32, &model.colliderMask,  1.0f);
        colliderDirty |= ImGui::IsItemDeactivatedAfterEdit();
      }

      if (colliderDirty && !scene.GetRegistry().all_of<ModelColliderDirty>(entity))
        scene.GetRegistry().emplace<ModelColliderDirty>(entity);
    }
  }

  static void SetCameraToEditorView(Scene& scene, Entity entity)
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

  static void DrawCamera(EditorContext& context, Entity entity, CameraComponent& cc)
  {
    if (!ImGui::CollapsingHeader(ICON_FA_VIDEO " Camera", ImGuiTreeNodeFlags_DefaultOpen))
      return;

    ImGui::PushID("Camera");

    float fovDegrees = glm::degrees(cc.fov);
    if (ImGui::DragFloat("FOV", &fovDegrees, 0.25f, 10.0f, 120.0f, "%.1f deg"))
      cc.fov = glm::radians(fovDegrees);

    ImGui::DragFloat("Near", &cc.nearPlane, 0.01f, 0.01f, 10.0f, "%.3f");
    ImGui::DragFloat("View distance", &cc.farPlane, 1.0f, 10.0f, 10000.0f, "%.1f");
    // Forced to the viewport every frame, so it is reported rather than offered
    ImGui::Text("Aspect: %.2f", cc.aspectRatio);

    if (!context.scene->HasComponent<EditorOnlyTag>(entity))
    {
      ImGui::Separator();
      if (context.previewCamera == entity)
      {
        if (ImGui::Button(ICON_FA_XMARK " Stop Preview"))
          context.StopCameraPreview();
      }
      else if (ImGui::Button(ICON_FA_VIDEO " Preview"))
      {
        context.StartCameraPreview(entity);
      }

      ImGui::SameLine();
      if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS " Set To View"))
        SetCameraToEditorView(*context.scene, entity);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Move this camera to the editor camera position");
    }

    ImGui::PopID();
  }

  static bool DrawCollider(ColliderComponent& collider)
  {
    ImGui::PushID("Collider");
    bool open = ImGui::CollapsingHeader(ICON_FA_CUBE " Collider", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
    if (ImGui::Button(ICON_FA_XMARK "##RemoveCollider", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
    {
      ImGui::PopStyleColor(3);
      ImGui::PopID();
      return true;
    }
    ImGui::PopStyleColor(3);

    if (open)
    {
      ImGui::DragFloat3("Local Offset", &collider.localOffset.x, 0.05f);
      ImGui::DragFloat3("Half Extents", &collider.halfExtents.x, 0.05f, 0.0f, 1000.0f);
      ImGui::Checkbox("Static", &collider.isStatic);

      ImGui::DragScalar("Layer", ImGuiDataType_U32, &collider.layer, 1.0f);
      ImGui::DragScalar("Mask",  ImGuiDataType_U32, &collider.mask,  1.0f);
    }

    ImGui::PopID();
    return false;
  }

  static bool DrawScatter(EditorContext& context, ScatterComponent& scatter)
  {
    ImGui::PushID("Scatter");
    bool open = ImGui::CollapsingHeader(ICON_FA_SEEDLING " Scatter", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
    if (ImGui::Button(ICON_FA_XMARK "##RemoveScatter", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
    {
      ImGui::PopStyleColor(3);
      ImGui::PopID();
      return true;
    }
    ImGui::PopStyleColor(3);

    if (open)
    {
      auto entity = context.selectedEntity;
      auto& scene = *context.scene;
      bool committed = false;
      const bool isSatellite = !scatter.clusterSource.empty();

      const char* meshTypes[] = { "Plane", "Model" };
      int meshType = static_cast<int>(scatter.meshType);
      if (ImGui::Combo("Mesh Type", &meshType, meshTypes, 2))
      {
        scatter.meshType = static_cast<ScatterMeshType>(meshType);
        committed = true;
      }

      if (scatter.meshType == ScatterMeshType::Plane)
      {
        ImGui::DragFloat("Plane Width", &scatter.planeWidth, 0.01f, 0.01f, 10.0f);
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Plane Height", &scatter.planeHeight, 0.01f, 0.01f, 10.0f);
        committed |= ImGui::IsItemDeactivatedAfterEdit();
      }

      if (scatter.meshType == ScatterMeshType::Plane)
      {
        if (!scatter.materialPath.empty())
        {
          auto relativePath = context.assetManager->MakeRelative(scatter.materialPath);
          ImGui::TextDisabled("Texture: %s", relativePath.c_str());
        }
        else
        {
          ImGui::TextDisabled("Texture: None");
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER_OPEN "##BrowseScatterTexture"))
        {
          nfdu8filteritem_t filters[] = { { "Image", "png,jpg,jpeg,tga,bmp" } };
          auto path = FileDialog::OpenFile(filters, 1);
          if (!path.empty())
          {
            scatter.materialPath = path;
            committed = true;
          }
        }
      }

      if (!isSatellite)
      {
        int count = static_cast<int>(scatter.count);
        ImGui::DragInt("Count", &count, 1.0f, 0, static_cast<int>(Render::MAX_INSTANCES));
        scatter.count = static_cast<uint32_t>(std::max(0, count));
        committed |= ImGui::IsItemDeactivatedAfterEdit();
      }

      int seed = scatter.seed;
      ImGui::DragInt("Seed", &seed);
      scatter.seed = seed;
      committed |= ImGui::IsItemDeactivatedAfterEdit();

      ImGui::DragFloat("Min Scale", &scatter.minScale, 0.01f, 0.01f, 10.0f);
      committed |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::DragFloat("Max Scale", &scatter.maxScale, 0.01f, 0.01f, 10.0f);
      committed |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::DragFloat("Max Slope", &scatter.maxSlope, 0.01f, 0.0f, 1.0f);
      committed |= ImGui::IsItemDeactivatedAfterEdit();
      if (ImGui::Checkbox("Random Y Rotation", &scatter.randomYRotation))
        committed = true;

      if (!isSatellite)
      {
        ImGui::DragFloat("Radius", &scatter.radius, 0.5f, 0.0f, 10000.0f);
        committed |= ImGui::IsItemDeactivatedAfterEdit();

        if (ImGui::Checkbox("Use Road Mask", &scatter.useRoadMask))
          committed = true;
        if (scatter.useRoadMask)
        {
          ImGui::DragFloat("Road Padding", &scatter.roadMaskPadding, 0.1f, 0.0f, 50.0f);
          committed |= ImGui::IsItemDeactivatedAfterEdit();
          ImGui::DragFloat("Outer Radius", &scatter.roadMaskOuterRadius, 0.5f, 0.0f, 200.0f);
          committed |= ImGui::IsItemDeactivatedAfterEdit();
          ImGui::DragFloat("Falloff", &scatter.roadMaskFalloff, 0.1f, 0.0f, 50.0f);
          committed |= ImGui::IsItemDeactivatedAfterEdit();
        }
      }

      ImGui::Separator();
      char clusterBuf[256] = {};
      std::memcpy(clusterBuf, scatter.clusterSource.c_str(),
        std::min(scatter.clusterSource.size(), sizeof(clusterBuf) - 1));
      if (ImGui::InputText("Cluster Source", clusterBuf, sizeof(clusterBuf)))
      {
        scatter.clusterSource = clusterBuf;
        committed = true;
      }
      if (isSatellite)
      {
        ImGui::DragFloat("Cluster Radius", &scatter.clusterRadius, 0.1f, 0.5f, 20.0f);
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        int cMin = static_cast<int>(scatter.clusterCountMin);
        int cMax = static_cast<int>(scatter.clusterCountMax);
        ImGui::DragInt("Cluster Count Min", &cMin, 0.1f, 0, 1000);
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragInt("Cluster Count Max", &cMax, 0.1f, 0, 1000);
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        scatter.clusterCountMin = static_cast<uint32_t>(std::max(0, cMin));
        scatter.clusterCountMax = static_cast<uint32_t>(std::max(static_cast<int>(scatter.clusterCountMin), cMax));
      }

      if (scatter.meshType != ScatterMeshType::Plane)
      {
        ImGui::Separator();
        ImGui::Text("Collider");

        if (ImGui::Checkbox("Enabled##Collider", &scatter.colliderEnabled))
          committed = true;

        if (scatter.colliderEnabled)
        {
          ImGui::DragFloat3("Offset##Collider", &scatter.colliderOffset.x, 0.05f);
          committed |= ImGui::IsItemDeactivatedAfterEdit();
          ImGui::DragFloat3("Half Extents Scale##Collider", &scatter.colliderHalfExtentsScale.x, 0.05f, 0.0f, 10.0f);
          committed |= ImGui::IsItemDeactivatedAfterEdit();

          if (ImGui::Checkbox("Static##Collider", &scatter.colliderIsStatic))
            committed = true;
          ImGui::DragScalar("Layer##Collider", ImGuiDataType_U32, &scatter.colliderLayer, 1.0f);
          committed |= ImGui::IsItemDeactivatedAfterEdit();
          ImGui::DragScalar("Mask##Collider",  ImGuiDataType_U32, &scatter.colliderMask,  1.0f);
          committed |= ImGui::IsItemDeactivatedAfterEdit();
        }
      }

      if (committed && !scene.GetRegistry().all_of<ScatterDirty>(entity))
        scene.GetRegistry().emplace<ScatterDirty>(entity);
    }
    ImGui::PopID();
    return false;
  }

  // Model subtree entities are rebuilt from the model file on every load, so anything
  // authored on them is stored as an override. Surface that state and let it be undone.
  static void DrawModelNode(EditorContext& context, Entity entity)
  {
    auto& scene = *context.scene;

    if (context.componentRegistry == nullptr || !scene.HasComponent<ModelNodeComponent>(entity))
      return;
    if (scene.GetComponent<ModelNodeComponent>(entity).nodeIndex == 0)
      return;

    auto& assets = *context.assetManager;
    auto& registry = *context.componentRegistry;

    bool nodeOverridden = ModelOverrides::IsNodeOverridden(scene, assets, registry, entity);
    bool materialOverridden = ModelOverrides::IsMaterialOverridden(scene, assets, entity);

    ImGui::TextDisabled(ICON_FA_CUBES " Model node");
    ImGui::SameLine();
    if (nodeOverridden)
      ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.2f, 1.0f), "overridden");
    else
      ImGui::TextDisabled("matches source");

    ImGui::BeginDisabled(!nodeOverridden);
    if (ImGui::Button(ICON_FA_ROTATE_LEFT " Revert node"))
      ModelOverrides::RevertNode(scene, assets, registry, entity);
    ImGui::EndDisabled();

    ImGui::SameLine();

    ImGui::BeginDisabled(!materialOverridden);
    if (ImGui::Button(ICON_FA_ROTATE_LEFT " Revert material"))
      ModelOverrides::RevertMaterial(scene, assets, entity);
    ImGui::EndDisabled();

    ImGui::Separator();
  }

  // Drawn for every entity rather than behind a component header: absence of the
  // component is a state too, and the effective line matters most exactly then.
  static void DrawBakeOverride(EditorContext& context, Entity entity)
  {
    auto& scene = *context.scene;
    auto& registry = scene.GetRegistry();

    ImGui::PushID("BakeOverride");

    // Scatter output is regenerated and never serialized, so an override set on it would
    // silently vanish on the next regeneration.
    const bool scatterInstance = registry.all_of<ScatterInstanceTag>(entity);

    const char* modes[] = { "Auto", "Include", "Exclude" };
    const auto* component = registry.try_get<BakeOverrideComponent>(entity);
    int mode = static_cast<int>(component != nullptr ? component->mode : BakeOverride::Auto);
    ImGui::BeginDisabled(scatterInstance);
    if (ImGui::Combo("Bake", &mode, modes, IM_ARRAYSIZE(modes)))
    {
      if (static_cast<BakeOverride>(mode) == BakeOverride::Auto)
        scene.RemoveComponent<BakeOverrideComponent>(entity);
      else
        registry.emplace_or_replace<BakeOverrideComponent>(entity,
          BakeOverrideComponent { .mode = static_cast<BakeOverride>(mode) });
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
      if (scatterInstance)
        ImGui::SetTooltip("Runtime scatter output, regenerated by its scatter parent.\n"
          "Set the override on the scatter parent; it covers this whole subtree.");
      else
        ImGui::SetTooltip("Auto leaves out anything with a dynamic collider on itself or an ancestor.\n"
          "Include and Exclude override that for this entity and everything below it;\n"
          "the nearest override up the hierarchy wins.");
    }

    if (registry.all_of<HiddenTag>(entity))
    {
      ImGui::TextDisabled("Effective: hidden - its meshes are not rendered into bakes, its lights still contribute");
    }
    else
    {
      const BakeInclusion inclusion = ResolveBakeInclusion(registry, entity);
      const char* verdict = inclusion.excluded ? "excluded" : "included";
      const bool byOverride = inclusion.reason != BakeInclusionReason::DynamicCollider;

      if (inclusion.reason == BakeInclusionReason::Default)
        ImGui::TextDisabled("Effective: %s", verdict);
      else if (inclusion.source == entity)
        ImGui::TextDisabled("Effective: %s by %s", verdict,
          byOverride ? "this override" : "its dynamic collider");
      else if (scene.HasComponent<Name>(inclusion.source))
        ImGui::TextDisabled("Effective: %s by the %s on '%s'", verdict,
          byOverride ? "override" : "dynamic collider", scene.GetName(inclusion.source).c_str());
      else
        ImGui::TextDisabled("Effective: %s by the %s on 'Entity %u'", verdict,
          byOverride ? "override" : "dynamic collider", static_cast<uint32_t>(inclusion.source));
    }

    ImGui::PopID();
  }

  void DetailsPanel::OnRender(EditorContext& context)
  {
    if (!ImGui::Begin("Details"))
    {
      ImGui::End();
      return;
    }

    if (!context.scene || context.selectedEntity == entt::null)
    {
      ImGui::TextDisabled("No entity selected");
      ImGui::End();
      return;
    }

    auto& scene = *context.scene;
    Entity entity = context.selectedEntity;

    if (scene.HasComponent<Name>(entity))
      ImGui::Text("%s", scene.GetName(entity).c_str());
    else
      ImGui::Text("Entity %d", (int)entity);

    const char* revealLabel = ICON_FA_SITEMAP " Show in Outliner";
    float revealWidth = ImGui::CalcTextSize(revealLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - revealWidth - ImGui::GetStyle().WindowPadding.x);
    if (ImGui::SmallButton(revealLabel))
      context.RevealEntity(entity);

    ImGui::Separator();

    DrawModelNode(context, entity);

    if (scene.HasComponent<LocalTransform>(entity))
      DrawTransform(context, scene.GetComponent<LocalTransform>(entity));

    if (scene.HasComponent<MeshComponent>(entity))
      DrawMesh(context, scene.GetComponent<MeshComponent>(entity));

    if (scene.HasComponent<MaterialComponent>(entity))
      DrawMaterial(context, scene.GetComponent<MaterialComponent>(entity));

    if (scene.HasComponent<LightComponent>(entity))
    {
      if (DrawLight(context, scene.GetComponent<LightComponent>(entity)))
        scene.RemoveComponent<LightComponent>(entity);
    }

    if (scene.HasComponent<ReflectionProbeComponent>(entity))
    {
      if (DrawReflectionProbe(context, scene.GetComponent<ReflectionProbeComponent>(entity)))
        scene.RemoveComponent<ReflectionProbeComponent>(entity);
    }

    if (scene.HasComponent<IrradianceVolumeComponent>(entity))
    {
      if (DrawIrradianceVolume(context, scene.GetComponent<IrradianceVolumeComponent>(entity)))
        scene.RemoveComponent<IrradianceVolumeComponent>(entity);
    }

    if (scene.HasComponent<TerrainComponent>(entity))
    {
      if (DrawTerrain(context, scene.GetComponent<TerrainComponent>(entity)))
      {
        scene.RemoveComponent<TerrainComponent>(entity);
        if (scene.HasComponent<TerrainDirty>(entity))
          scene.RemoveComponent<TerrainDirty>(entity);
        if (scene.HasComponent<TerrainMaterialComponent>(entity))
          scene.RemoveComponent<TerrainMaterialComponent>(entity);
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
    }

    if (scene.HasComponent<RoadComponent>(entity))
    {
      if (DrawRoad(context, scene.GetComponent<RoadComponent>(entity)))
      {
        scene.RemoveComponent<RoadComponent>(entity);
        if (scene.HasComponent<RoadDirty>(entity))
          scene.RemoveComponent<RoadDirty>(entity);
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
    }

    if (scene.HasComponent<ScatterComponent>(entity))
    {
      if (DrawScatter(context, scene.GetComponent<ScatterComponent>(entity)))
      {
        scene.RemoveComponent<ScatterComponent>(entity);
        if (scene.HasComponent<ScatterDirty>(entity))
          scene.RemoveComponent<ScatterDirty>(entity);
      }
    }

    if (scene.HasComponent<TerrainMaterialComponent>(entity))
    {
      auto& tm = scene.GetComponent<TerrainMaterialComponent>(entity);
      ImGui::PushID("TerrainMaterial");
      if (ImGui::CollapsingHeader(ICON_FA_LAYER_GROUP " Terrain Material", ImGuiTreeNodeFlags_DefaultOpen))
      {
        ImGui::DragFloat("Slope Start", &tm.slopeStart, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Slope End", &tm.slopeEnd, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Layer 1 UV Scale", &tm.layer1UvScale, 0.1f, 0.1f, 100.0f);

        ImGui::Separator();
        ImGui::Text("Layer 2 (Shoulder/Gravel)");
        ImGui::DragFloat("Layer 2 UV Scale", &tm.layer2UvScale, 0.1f, 0.1f, 100.0f);
        ImGui::ColorEdit3("Layer 2 Tint", &tm.layer2Tint.x);
        ImGui::DragFloat("Layer 2 Roughness", &tm.layer2RoughnessFactor, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("Layer 2 Metallic", &tm.layer2MetallicFactor, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("Shoulder Inner Radius", &tm.shoulderInnerRadius, 0.1f, 0.0f, 50.0f);
        ImGui::DragFloat("Shoulder Outer Radius", &tm.shoulderOuterRadius, 0.1f, 0.0f, 50.0f);
        ImGui::DragFloat("Shoulder Warp Amplitude", &tm.shoulderWarpAmplitude, 0.05f, 0.0f, 20.0f);
        ImGui::DragFloat("Shoulder Warp Scale", &tm.shoulderWarpScale, 0.005f, 0.001f, 1.0f);
      }
      ImGui::PopID();
    }

    if (scene.HasComponent<ModelSourceComponent>(entity))
      DrawModel(context, scene.GetComponent<ModelSourceComponent>(entity));

    if (scene.HasComponent<CameraComponent>(entity))
      DrawCamera(context, entity, scene.GetComponent<CameraComponent>(entity));

    if (scene.HasComponent<ColliderComponent>(entity))
    {
      if (DrawCollider(scene.GetComponent<ColliderComponent>(entity)))
        scene.RemoveComponent<ColliderComponent>(entity);
    }

    ImGui::Separator();
    DrawBakeOverride(context, entity);

    ImGui::Separator();

    float buttonWidth = ImGui::GetContentRegionAvail().x;
    if (ImGui::Button(ICON_FA_CIRCLE_PLUS " Add Component", ImVec2(buttonWidth, 0)))
      ImGui::OpenPopup("AddComponentPopup");

    if (ImGui::BeginPopup("AddComponentPopup"))
    {
      if (!scene.HasComponent<LightComponent>(entity))
      {
        if (ImGui::MenuItem(ICON_FA_LIGHTBULB " Light"))
          scene.AddComponent<LightComponent>(entity);
      }

      if (!scene.HasComponent<ReflectionProbeComponent>(entity))
      {
        if (ImGui::MenuItem(ICON_FA_GLOBE " Reflection Probe"))
          scene.AddComponent<ReflectionProbeComponent>(entity);
      }

      if (!scene.HasComponent<IrradianceVolumeComponent>(entity))
      {
        if (ImGui::MenuItem(ICON_FA_CUBES " Irradiance Volume"))
          scene.AddComponent<IrradianceVolumeComponent>(entity);
      }

      if (!scene.HasComponent<TerrainComponent>(entity))
      {
        if (ImGui::MenuItem(ICON_FA_MOUNTAIN " Terrain"))
        {
          scene.AddComponent<TerrainComponent>(entity);
          if (!scene.HasComponent<MaterialComponent>(entity))
            scene.AddComponent<MaterialComponent>(entity, context.assetManager->FindOrCreateDefaultMaterial());
          scene.GetRegistry().emplace_or_replace<TerrainDirty>(entity);
        }
      }

      if (!scene.HasComponent<RoadComponent>(entity))
      {
        if (ImGui::MenuItem(ICON_FA_ROAD " Road"))
        {
          scene.AddComponent<RoadComponent>(entity);
          if (!scene.HasComponent<MaterialComponent>(entity))
            scene.AddComponent<MaterialComponent>(entity, context.assetManager->FindOrCreateDefaultMaterial());
          scene.GetRegistry().emplace_or_replace<RoadDirty>(entity);
        }
      }

      if (!scene.HasComponent<ScatterComponent>(entity))
      {
        if (ImGui::MenuItem(ICON_FA_SEEDLING " Scatter"))
        {
          scene.AddComponent<ScatterComponent>(entity);
          scene.GetRegistry().emplace_or_replace<ScatterDirty>(entity);
        }
      }

      if (!scene.HasComponent<ColliderComponent>(entity))
      {
        if (ImGui::MenuItem(ICON_FA_CUBE " Collider"))
          scene.AddComponent<ColliderComponent>(entity);
      }

      ImGui::EndPopup();
    }

    ImGui::End();
  }
}
