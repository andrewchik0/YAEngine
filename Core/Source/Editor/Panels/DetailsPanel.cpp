#include "Editor/Panels/DetailsPanel.h"

#include <imgui.h>

#include "Application.h"
#include "Editor/EditorContext.h"
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

    // X
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

    // Y
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

    // Z
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

  static void DrawTransform(EditorContext& context, TransformComponent& tc)
  {
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
      bool dirty = false;

      if (ColoredDragFloat3("Position", &tc.position.x, 0.1f, 0.0f, 0.0f, 0.0f))
        dirty = true;

      glm::vec3 euler = glm::degrees(glm::eulerAngles(tc.rotation));
      if (ColoredDragFloat3("Rotation", &euler.x, 0.5f, 0.0f, 0.0f, 0.0f))
      {
        tc.rotation = glm::quat(glm::radians(euler));
        dirty = true;
      }

      if (ColoredDragFloat3("Scale", &tc.scale.x, 0.1f, 1.0f, 0.001f, 1000.0f))
        dirty = true;

      ImGui::Text("MinBB: (%.1f, %.1f, %.1f)", tc.minBB.x, tc.minBB.y, tc.minBB.z);
      ImGui::Text("MaxBB: (%.1f, %.1f, %.1f)", tc.maxBB.x, tc.maxBB.y, tc.maxBB.z);

      if (dirty)
        context.scene->MarkDirty(context.selectedEntity);
    }
  }

  static void DrawMesh(EditorContext& context, MeshComponent& mc)
  {
    if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::Checkbox("Render", &mc.shouldRender);
      ImGui::Checkbox("Double Sided", &mc.doubleSided);
      ImGui::Checkbox("No Shading", &mc.noShading);

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
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
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
          bool isSelected = (mc.asset == handle);
          if (ImGui::Selectable(mat.name.c_str(), isSelected))
            mc.asset = handle;

          if (isSelected)
            ImGui::SetItemDefaultFocus();
        });

        ImGui::EndCombo();
      }

      if (materials.Has(mc.asset))
      {
        if (ImGui::Button("Select in Browser"))
          context.SelectMaterial(mc.asset);
      }
    }
  }

  static void DrawCamera(CameraComponent& cc)
  {
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
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

    if (scene.HasComponent<TransformComponent>(entity))
      DrawTransform(context, scene.GetComponent<TransformComponent>(entity));

    if (scene.HasComponent<MeshComponent>(entity))
      DrawMesh(context, scene.GetComponent<MeshComponent>(entity));

    if (scene.HasComponent<MaterialComponent>(entity))
      DrawMaterial(context, scene.GetComponent<MaterialComponent>(entity));

    if (scene.HasComponent<CameraComponent>(entity))
      DrawCamera(scene.GetComponent<CameraComponent>(entity));

    ImGui::End();
  }
}
