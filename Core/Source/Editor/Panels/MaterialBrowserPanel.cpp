#include "Editor/Panels/MaterialBrowserPanel.h"

#include <imgui.h>

#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorIcons.h"
#include "Assets/AssetManager.h"
#include "Editor/Utils/EditorTextureCache.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"

namespace YAEngine
{
  static bool ContainsCaseInsensitive(std::string_view haystack, std::string_view needle)
  {
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
      [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
      });
    return it != haystack.end();
  }

  void MaterialBrowserPanel::BeginRename(MaterialHandle handle, const std::string& currentName)
  {
    m_RenamingMaterial = handle;
    snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", currentName.c_str());
    b_RenameNeedsFocus = true;
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
      bool isRenaming = (m_RenamingMaterial == handle);

      if (isRenaming)
      {
        if (b_RenameNeedsFocus)
          ImGui::SetKeyboardFocusHere();

        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##matRename", m_RenameBuffer, sizeof(m_RenameBuffer),
          ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
        {
          if (m_RenameBuffer[0] != '\0')
            mat.name = m_RenameBuffer;
          m_RenamingMaterial = MaterialHandle::Invalid();
          b_RenameNeedsFocus = false;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
          m_RenamingMaterial = MaterialHandle::Invalid();
          b_RenameNeedsFocus = false;
        }
        else if (b_RenameNeedsFocus)
        {
          if (ImGui::IsItemActive())
            b_RenameNeedsFocus = false;
        }
        else if (!ImGui::IsItemActive() && !ImGui::IsItemFocused())
        {
          if (m_RenameBuffer[0] != '\0')
            mat.name = m_RenameBuffer;
          m_RenamingMaterial = MaterialHandle::Invalid();
        }
      }
      else
      {
        if (ImGui::Selectable(mat.name.c_str(), isSelected))
          context.SelectMaterial(handle);

        if (ImGui::BeginPopupContextItem())
        {
          if (ImGui::MenuItem(ICON_FA_PEN " Rename"))
            BeginRename(handle, mat.name);

          if (ImGui::MenuItem(ICON_FA_CLONE " Duplicate"))
          {
            MaterialHandle newHandle = materials.Duplicate(handle);
            context.SelectMaterial(newHandle);
          }

          if (ImGui::MenuItem(ICON_FA_TRASH_CAN " Delete"))
          {
            m_PendingDelete = handle;
          }

          ImGui::EndPopup();
        }
      }

      ImGui::PopID();
    }

    if (m_PendingDelete != MaterialHandle::Invalid())
    {
      if (context.selectedMaterial == m_PendingDelete)
        context.ClearMaterialSelection();

      auto view = context.scene->GetView<MaterialComponent>();
      for (auto entity : view)
      {
        auto& mc = context.scene->GetComponent<MaterialComponent>(entity);
        if (mc.asset == m_PendingDelete)
          context.scene->RemoveComponent<MaterialComponent>(entity);
      }

      materials.Destroy(m_PendingDelete);
      m_PendingDelete = MaterialHandle::Invalid();
    }

    if (ImGui::BeginPopupContextWindow("##MatBrowserCtx", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight))
    {
      if (ImGui::MenuItem(ICON_FA_CIRCLE_PLUS " Create New"))
      {
        MaterialHandle h = materials.Create();
        context.SelectMaterial(h);
        BeginRename(h, materials.Get(h).name);
      }
      ImGui::EndPopup();
    }

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
      context.ClearMaterialSelection();

    ImGui::End();
  }
}
