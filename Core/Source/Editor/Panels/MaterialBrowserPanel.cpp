#include "Editor/Panels/MaterialBrowserPanel.h"

#include <imgui.h>

#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorIcons.h"
#include "Assets/AssetManager.h"
#include "Editor/Utils/EditorTextureCache.h"

namespace YAEngine
{
  static bool ContainsCaseInsensitive(const std::string& str, const char* substr)
  {
    if (substr[0] == '\0')
      return true;

    auto toLower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };

    std::string lowerStr(str.size(), '\0');
    std::transform(str.begin(), str.end(), lowerStr.begin(), toLower);

    std::string lowerSub(substr);
    std::transform(lowerSub.begin(), lowerSub.end(), lowerSub.begin(), toLower);

    return lowerStr.find(lowerSub) != std::string::npos;
  }

  void MaterialBrowserPanel::OnRender(EditorContext& context)
  {
    if (!ImGui::Begin("Materials"))
    {
      ImGui::End();
      return;
    }

    if (!context.assetManager)
    {
      ImGui::TextDisabled("Asset manager not available");
      ImGui::End();
      return;
    }

    auto& materials = context.assetManager->Materials();

    if (ImGui::Button(ICON_FA_CIRCLE_PLUS " Create"))
    {
      MaterialHandle h = materials.Create();
      context.SelectMaterial(h);
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##matfilter", ICON_FA_MAGNIFYING_GLASS " Search...", m_FilterText, sizeof(m_FilterText));

    ImGui::Separator();

    struct MatEntry
    {
      MaterialHandle handle;
      Material* mat;
    };

    std::vector<MatEntry> sorted;
    materials.ForEachWithHandle([&](MaterialHandle handle, Material& mat)
    {
      if (!ContainsCaseInsensitive(mat.name, m_FilterText))
        return;
      sorted.push_back({ handle, &mat });
    });

    std::sort(sorted.begin(), sorted.end(), [](const MatEntry& a, const MatEntry& b)
    {
      return a.mat->name < b.mat->name;
    });

    for (auto& [handle, matPtr] : sorted)
    {
      auto& mat = *matPtr;

      ImGui::PushID(static_cast<int>(handle.index));

      ImGui::ColorButton("##albedo", ImVec4(mat.albedo.x, mat.albedo.y, mat.albedo.z, 1.0f),
        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(20, 20));

      ImGui::SameLine();

      if (mat.baseColorTexture && context.textureCache)
      {
        VkDescriptorSet ds = context.textureCache->GetOrRegister(mat.baseColorTexture);
        if (ds != VK_NULL_HANDLE)
        {
          ImGui::Image((void*)ds, ImVec2(20, 20));
          ImGui::SameLine();
        }
      }

      bool isSelected = (context.selectedMaterial == handle);
      if (ImGui::Selectable(mat.name.c_str(), isSelected))
        context.SelectMaterial(handle);

      if (ImGui::BeginPopupContextItem())
      {
        if (ImGui::MenuItem(ICON_FA_CLONE " Duplicate"))
        {
          MaterialHandle newHandle = materials.Duplicate(handle);
          context.SelectMaterial(newHandle);
        }
        ImGui::EndPopup();
      }

      ImGui::PopID();
    }

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
      context.ClearMaterialSelection();

    ImGui::End();
  }
}
