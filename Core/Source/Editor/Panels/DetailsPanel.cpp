#include "Editor/Panels/DetailsPanel.h"

#include <imgui.h>

#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorIcons.h"
#include "Editor/Utils/FileDialog.h"
#include "Editor/Utils/CurveEditor.h"
#include "Editor/Utils/SplinePathEditor.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
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
        ImGui::SetTooltip("Reproject reflections onto the influence volume.\nOnly correct while the volume matches the real geometry.");

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

  // Overlapping volumes share nodes on the world lattice, and a shared node is
  // only really shared when both bakes captured it the same way. A different
  // capture resolution means a different SH projection error, so the two volumes
  // disagree by that error exactly where they are supposed to agree. Warn, never
  // block - the artist may well want a cheap outer volume anyway.
  static void DrawVolumeResolutionConflicts(EditorContext& context,
    const IrradianceVolumeComponent& iv, const glm::vec3& center, const glm::quat& rotation)
  {
    glm::vec3 half = ComputeRotatedBoxAabbHalfExtents(rotation, iv.halfExtents);
    glm::vec3 selfMin = center - half;
    glm::vec3 selfMax = center + half;

    auto view = context.scene->GetView<IrradianceVolumeComponent, WorldTransform>();
    for (auto entity : view)
    {
      if (entity == context.selectedEntity) continue;

      auto& other = view.get<IrradianceVolumeComponent>(entity);
      if (other.captureResolution == iv.captureResolution) continue;

      auto& wt = view.get<WorldTransform>(entity);
      glm::vec3 otherCenter = glm::vec3(wt.world[3]);
      glm::vec3 otherHalf = ComputeRotatedBoxAabbHalfExtents(
        ExtractIrradianceBoxRotation(wt.world), other.halfExtents);

      glm::vec3 otherMin = otherCenter - otherHalf;
      glm::vec3 otherMax = otherCenter + otherHalf;
      if (glm::any(glm::greaterThan(selfMin, otherMax))) continue;
      if (glm::any(glm::greaterThan(otherMin, selfMax))) continue;

      ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
        "Overlaps '%s' at capture resolution %u, not %u -\nshared nodes will not match. Use the same resolution.",
        context.scene->GetName(entity).c_str(), other.captureResolution, iv.captureResolution);
    }
  }

  static bool DrawIrradianceVolume(EditorContext& context, IrradianceVolumeComponent& iv)
  {
    // Node counts that make bake time explode. One node is six offscreen renders,
    // so the count, not memory, is what has to stay in check.
    constexpr uint32_t WARN_NODE_COUNT = 8192;
    constexpr uint32_t MAX_NODE_COUNT = BakeLimits::VOLUME_MAX_NODE_COUNT;
    constexpr const char* GRID_COST_TOOLTIP =
      "16x8x16 = 2048 nodes = 12288 face renders - tens of seconds, fine.\n"
      "64x32x64 = 131072 nodes - hours. Raise the spacing instead.";

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
      // so what the combo shows is also what gets saved.
      iv.spacing = SnapIrradianceSpacing(iv.spacing);
      int spacingIdx = 0;
      for (int i = 0; i < IM_ARRAYSIZE(spacingOptions); i++)
        if (IRRADIANCE_SPACINGS[i] == iv.spacing) spacingIdx = i;
      if (ImGui::Combo("Spacing", &spacingIdx, spacingOptions, IM_ARRAYSIZE(spacingOptions)))
        iv.spacing = IRRADIANCE_SPACINGS[spacingIdx];
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Node spacing in meters, exact and never recomputed.\n"
          "Nodes sit on one world lattice shared by every volume, so the box is\n"
          "snapped to the spacing instead. Overlapping volumes then agree.");

      const char* resOptions[] = { "16", "32", "64" };
      uint32_t resValues[] = { 16, 32, 64 };
      int resIdx = -1;
      for (int i = 0; i < 3; i++)
        if (resValues[i] == iv.captureResolution) resIdx = i;

      // Snapped instead of silently displayed as something else: a value outside
      // the three options would otherwise show as "32" while the component kept
      // the original, and the resolution conflict warning compares the raw values.
      if (resIdx < 0)
      {
        resIdx = 1;
        iv.captureResolution = resValues[resIdx];
      }
      if (ImGui::Combo("Capture Resolution", &resIdx, resOptions, IM_ARRAYSIZE(resOptions)))
        iv.captureResolution = resValues[resIdx];

      // Node count is shown BEFORE baking - it is the only warning the user gets
      // before committing to minutes of offscreen rendering. The lattice covers
      // the world-space AABB of the rotated box, so the entity transform is part
      // of the count and has to be fed in the same way the baker feeds it.
      const glm::mat4& volumeWorld = context.scene->GetWorldTransform(context.selectedEntity).world;
      glm::vec3 center = glm::vec3(volumeWorld[3]);
      glm::quat rotation = ExtractIrradianceBoxRotation(volumeWorld);

      IrradianceGridLayout layout = ComputeIrradianceGridLayout(center, rotation,
        iv.halfExtents, iv.spacing);
      uint32_t nodeCount = layout.GetNodeCount();

      ImGui::Separator();
      if (nodeCount > WARN_NODE_COUNT)
      {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Grid: %ux%ux%u = %u nodes (%u face renders)",
          layout.nodeCounts.x, layout.nodeCounts.y, layout.nodeCounts.z,
          nodeCount, layout.GetFaceRenderCount());
        // Tooltip attached here, before the second line: IsItemHovered() refers to
        // the last item, so putting it after the warning bound it to the wrong one.
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", GRID_COST_TOOLTIP);
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Warning: bake will take many minutes");
      }
      else
      {
        ImGui::Text("Grid: %ux%ux%u = %u nodes (%u face renders)",
          layout.nodeCounts.x, layout.nodeCounts.y, layout.nodeCounts.z,
          nodeCount, layout.GetFaceRenderCount());
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", GRID_COST_TOOLTIP);
      }

      // A rotated box costs nodes: the lattice is world axis aligned, so it has to
      // cover the AABB. Roughly twice as many at 45 degrees around one axis.
      if (std::abs(rotation.w) < 0.9999f)
        ImGui::TextDisabled("Box is rotated - the lattice covers its world AABB, so\nthere are more nodes than an unrotated box would need.");

      DrawVolumeResolutionConflicts(context, iv, center, rotation);

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

      bool tooDense = nodeCount > MAX_NODE_COUNT;
      ImGui::BeginDisabled(tooDense);
      if (ImGui::Button(ICON_FA_CIRCLE_PLAY " Bake", ImVec2(-1, 0)))
        context.render->BakeIrradianceVolume(context.selectedEntity, *context.scene, *context.assetManager);
      ImGui::EndDisabled();

      if (tooDense)
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
          "Grid above %u nodes - raise the spacing to bake", MAX_NODE_COUNT);
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

      // XZ spline editor using SplinePathEditor
      std::vector<glm::vec2> points2D;
      points2D.reserve(road.points.size());

      // Find XZ bounds for normalization
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

      // Sync point count changes from editor
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

  static void SetCombinedTexturesRecursive(Scene& scene, AssetManager& assets, Entity entity, bool value)
  {
    if (scene.HasComponent<MaterialComponent>(entity))
    {
      auto& mc = scene.GetComponent<MaterialComponent>(entity);
      if (assets.Materials().Has(mc.asset))
      {
        auto& mat = assets.Materials().Get(mc.asset);
        mat.combinedTextures = value;
        mat.MarkChanged();
      }
    }

    auto& hc = scene.GetComponent<HierarchyComponent>(entity);
    Entity child = hc.firstChild;
    while (child != entt::null)
    {
      SetCombinedTexturesRecursive(scene, assets, child, value);
      child = scene.GetComponent<HierarchyComponent>(child).nextSibling;
    }
  }

  static void DrawModel(EditorContext& context, ModelSourceComponent& model)
  {
    if (ImGui::CollapsingHeader(ICON_FA_FILE_IMPORT " Model", ImGuiTreeNodeFlags_DefaultOpen))
    {
      auto relativePath = context.assetManager->MakeRelative(model.path);
      ImGui::Text("Path: %s", relativePath.c_str());

      if (ImGui::Checkbox("Combined Textures", &model.combinedTextures))
      {
        SetCombinedTexturesRecursive(*context.scene, *context.assetManager,
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

  static void DrawCamera(CameraComponent& cc)
  {
    if (ImGui::CollapsingHeader(ICON_FA_VIDEO " Camera", ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::Text("FOV: %.1f", glm::degrees(cc.fov));
      ImGui::Text("Aspect: %.2f", cc.aspectRatio);
      ImGui::Text("Near: %.3f", cc.nearPlane);
      ImGui::Text("Far: %.1f", cc.farPlane);
    }
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

    // Entity name header
    if (scene.HasComponent<Name>(entity))
      ImGui::Text("%s", scene.GetName(entity).c_str());
    else
      ImGui::Text("Entity %d", (int)entity);

    ImGui::Separator();

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
      DrawCamera(scene.GetComponent<CameraComponent>(entity));

    if (scene.HasComponent<ColliderComponent>(entity))
    {
      if (DrawCollider(scene.GetComponent<ColliderComponent>(entity)))
        scene.RemoveComponent<ColliderComponent>(entity);
    }

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
