#include "Editor/Panels/DetailsPanel.h"

#include <imgui.h>

#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorIcons.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Assets/AssetManager.h"

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
    }

    return false;
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

      ImGui::EndPopup();
    }

    ImGui::End();
  }
}
