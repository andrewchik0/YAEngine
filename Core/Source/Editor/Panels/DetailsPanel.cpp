#include "Editor/Panels/DetailsPanel.h"

#include <imgui.h>

#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorIcons.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Assets/AssetManager.h"
#include "Render/Render.h"

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
      if (materials.Has(mc.asset))
      {
        auto& mat = materials.Get(mc.asset);
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

      if (materials.Has(mc.asset))
      {
        auto& mat = materials.Get(mc.asset);

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
    }

    return false;
  }

  static bool DrawLightProbe(EditorContext& context, LightProbeComponent& lp)
  {
    ImGui::PushID("LightProbe");
    bool open = ImGui::CollapsingHeader(ICON_FA_GLOBE " Light Probe", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

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
      }

      ImGui::DragFloat("Fade Distance", &lp.fadeDistance, 0.1f, 0.0f, 100.0f);
      ImGui::DragInt("Priority", &lp.priority, 1, -100, 100);

      int res = static_cast<int>(lp.resolution);
      const char* resOptions[] = { "64", "128", "256", "512" };
      int resValues[] = { 64, 128, 256, 512 };
      int resIdx = 1;
      for (int i = 0; i < 4; i++)
        if (resValues[i] == res) resIdx = i;

      if (ImGui::Combo("Resolution", &resIdx, resOptions, IM_ARRAYSIZE(resOptions)))
        lp.resolution = static_cast<uint32_t>(resValues[resIdx]);

      if (lp.baked)
      {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "Baked (slot %u)", lp.atlasSlot);
        if (!lp.bakedIrradiancePath.empty())
          ImGui::TextDisabled("%s", lp.bakedIrradiancePath.c_str());

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
          if (ImGui::Begin("Probe Preview", &s_ProbePreviewOpen))
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

            ImGui::SeparatorText("Irradiance");
            for (uint32_t row = 0; row < 2; row++)
            {
              for (uint32_t col = 0; col < 3; col++)
              {
                uint32_t face = row * 3 + col;
                VkDescriptorSet ds = atlas.GetIrradianceFacePreview(ctx, lp.atlasSlot, face);
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

    if (scene.HasComponent<LightProbeComponent>(entity))
    {
      if (DrawLightProbe(context, scene.GetComponent<LightProbeComponent>(entity)))
        scene.RemoveComponent<LightProbeComponent>(entity);
    }

    if (scene.HasComponent<TerrainComponent>(entity))
    {
      if (DrawTerrain(context, scene.GetComponent<TerrainComponent>(entity)))
      {
        scene.RemoveComponent<TerrainComponent>(entity);
        if (scene.HasComponent<TerrainDirty>(entity))
          scene.RemoveComponent<TerrainDirty>(entity);
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

    if (scene.HasComponent<ModelSourceComponent>(entity))
      DrawModel(context, scene.GetComponent<ModelSourceComponent>(entity));

    if (scene.HasComponent<CameraComponent>(entity))
      DrawCamera(scene.GetComponent<CameraComponent>(entity));

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

      if (!scene.HasComponent<LightProbeComponent>(entity))
      {
        if (ImGui::MenuItem(ICON_FA_GLOBE " Light Probe"))
          scene.AddComponent<LightProbeComponent>(entity);
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

      ImGui::EndPopup();
    }

    ImGui::End();
  }
}
