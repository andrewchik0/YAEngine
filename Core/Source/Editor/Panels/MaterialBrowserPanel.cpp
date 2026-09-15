#include "Editor/Panels/MaterialBrowserPanel.h"

#include <imgui.h>
#include <imgui_internal.h>

#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorIcons.h"
#include "Editor/Utils/EditorStyle.h"
#include "Editor/Utils/EditorTextureCache.h"
#include "Editor/Utils/EditorWidgets.h"
#include "Assets/AssetManager.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Utils/StringSearch.h"

namespace YAEngine
{
  namespace
  {
    // A rename outside this panel (Material Inspector, the bridge) changes no count, so the cached
    // names are compared with the materials this often instead of every frame
    constexpr double NAME_CHECK_INTERVAL = 0.5;
  }

  void MaterialBrowserPanel::RefreshEntries(MaterialManager& materials)
  {
    const double now = ImGui::GetTime();
    bool stale = b_EntriesDirty || materials.Size() != m_Entries.size();
    if (!stale && now - m_EntriesCheckedAt >= NAME_CHECK_INTERVAL)
    {
      m_EntriesCheckedAt = now;
      for (size_t i = 0; i < m_Entries.size() && !stale; i++)
      {
        const Material* material = materials.TryGet(m_Entries[i].handle);
        stale = material == nullptr || material->name != m_Entries[i].name;
      }
    }

    if (stale)
    {
      b_EntriesDirty = false;
      m_EntriesCheckedAt = now;
      m_Entries.clear();
      materials.ForEachWithHandle([this](MaterialHandle handle, Material& material)
      {
        m_Entries.push_back(Entry { .handle = handle, .name = material.name });
      });
      std::sort(m_Entries.begin(), m_Entries.end(), [](const Entry& a, const Entry& b)
      {
        return a.name != b.name ? a.name < b.name : a.handle.index < b.handle.index;
      });
      b_VisibleDirty = true;
    }

    if (b_VisibleDirty || m_VisibleFilter != m_FilterText)
    {
      m_VisibleFilter = m_FilterText;
      b_VisibleDirty = false;
      m_Visible.clear();
      for (size_t i = 0; i < m_Entries.size(); i++)
      {
        if (ContainsCaseInsensitive(m_Entries[i].name, m_VisibleFilter))
          m_Visible.push_back(uint32_t(i));
      }
    }
  }

  void MaterialBrowserPanel::BeginRename(MaterialHandle handle, const std::string& currentName)
  {
    m_RenamingMaterial = handle;
    snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", currentName.c_str());
    b_RenameNeedsFocus = true;
    m_ScrollTarget = handle;
  }

  void MaterialBrowserPanel::FinishRename(MaterialManager& materials, bool commit)
  {
    Material* material = materials.TryGet(m_RenamingMaterial);
    if (commit && material != nullptr && m_RenameBuffer[0] != '\0')
    {
      material->name = m_RenameBuffer;
      // The new name sorts it elsewhere
      b_EntriesDirty = true;
      m_ScrollTarget = m_RenamingMaterial;
    }

    m_RenamingMaterial = MaterialHandle::Invalid();
    b_RenameNeedsFocus = false;
  }

  void MaterialBrowserPanel::CreateMaterial(EditorContext& context, MaterialManager& materials)
  {
    const MaterialHandle handle = materials.Create();
    context.SelectMaterial(handle);
    // The new name rarely matches the search, and a row the filter hides cannot be renamed
    m_FilterText[0] = '\0';
    BeginRename(handle, materials.Get(handle).name);
  }

  void MaterialBrowserPanel::DrawBackgroundInput(EditorContext& context, MaterialManager& materials)
  {
    if (ImGui::BeginPopupContextWindow("##MatBrowserCtx", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight))
    {
      if (ImGui::MenuItemEx("Create Material", ICON_LC_CIRCLE_PLUS))
        CreateMaterial(context, materials);
      ImGui::EndPopup();
    }

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
      context.ClearMaterialSelection();
  }

  void MaterialBrowserPanel::DrawRow(EditorContext& context, MaterialManager& materials, const Entry& entry, float rowHeight)
  {
    ImGui::PushID(int32_t(entry.handle.index));
    const ImVec2 rowStart = ImGui::GetCursorScreenPos();

    Material* material = materials.TryGet(entry.handle);
    if (material == nullptr)
    {
      // Destroyed after the list was built; the row keeps its height so the clipper stays in step
      ImGui::Dummy(ImVec2(1.0f, rowHeight));
      ImGui::PopID();
      return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float swatchSize = rowHeight - style.FramePadding.y;
    const float swatchY = rowStart.y + (rowHeight - swatchSize) * 0.5f;
    float x = rowStart.x;

    // Round and drawn by hand: ImGui's color button takes the square frame of a checkbox, which the mostly
    // white albedos then looked like. The invisible item keeps a click on the swatch from counting as a
    // click on empty space.
    const ImVec2 swatchCenter(x + swatchSize * 0.5f, swatchY + swatchSize * 0.5f);
    const float swatchRadius = swatchSize * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(x, swatchY));
    ImGui::InvisibleButton("##albedo", ImVec2(swatchSize, swatchSize));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddCircleFilled(swatchCenter, swatchRadius,
      ImGui::GetColorU32(ImVec4(material->albedo.x, material->albedo.y, material->albedo.z, 1.0f)));
    drawList->AddCircle(swatchCenter, swatchRadius, ImGui::GetColorU32(ToImGuiColor(EditorStyle::GetTheme().borderStrong)));
    x += swatchSize + style.ItemInnerSpacing.x;

    // Only rows the clipper draws reach here, so textures of rows scrolled away are never registered
    if (material->baseColorTexture && context.textureCache)
    {
      VkDescriptorSet descriptorSet = context.textureCache->GetOrRegister(material->baseColorTexture);
      if (descriptorSet != VK_NULL_HANDLE)
      {
        ImGui::SetCursorScreenPos(ImVec2(x, swatchY));
        ImGui::Image((void*)descriptorSet, ImVec2(swatchSize, swatchSize));
      }
    }
    // Reserved without a texture too, so the names line up
    x += swatchSize + style.ItemInnerSpacing.x;

    ImGui::SetCursorScreenPos(ImVec2(x, rowStart.y));
    if (entry.handle == m_RenamingMaterial)
    {
      if (b_RenameNeedsFocus)
        ImGui::SetKeyboardFocusHere();

      ImGui::SetNextItemWidth(-FLT_MIN);
      const bool entered = ImGui::InputText("##matRename", m_RenameBuffer, sizeof(m_RenameBuffer),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

      if (entered)
        FinishRename(materials, true);
      else if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        FinishRename(materials, false);
      else if (b_RenameNeedsFocus)
        b_RenameNeedsFocus = !ImGui::IsItemActive();
      else if (!ImGui::IsItemActive() && !ImGui::IsItemFocused())
        FinishRename(materials, true);
    }
    else
    {
      ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
      if (ImGui::Selectable(material->name.c_str(), context.selectedMaterial == entry.handle, ImGuiSelectableFlags_None,
        ImVec2(0.0f, rowHeight)))
      {
        context.SelectMaterial(entry.handle);
      }
      ImGui::PopStyleVar();

      if (ImGui::BeginPopupContextItem())
      {
        if (ImGui::MenuItemEx("Rename", ICON_LC_PENCIL))
          BeginRename(entry.handle, material->name);

        if (ImGui::MenuItemEx("Duplicate", ICON_LC_COPY))
        {
          const MaterialHandle copy = materials.Duplicate(entry.handle);
          context.SelectMaterial(copy);
          m_ScrollTarget = copy;
        }

        if (ImGui::MenuItemEx("Delete", ICON_LC_TRASH_2))
          m_PendingDelete = entry.handle;

        ImGui::EndPopup();
      }
    }

    if (entry.handle == m_ScrollTarget)
    {
      m_ScrollTarget = MaterialHandle::Invalid();
      const float windowTop = ImGui::GetWindowPos().y;
      if (rowStart.y < windowTop || rowStart.y + rowHeight > windowTop + ImGui::GetWindowHeight())
        ImGui::SetScrollFromPosY(rowStart.y + rowHeight * 0.5f - windowTop, 0.5f);
    }

    ImGui::PopID();
  }

  void MaterialBrowserPanel::DrawList(EditorContext& context, MaterialManager& materials)
  {
    int32_t scrollIndex = -1;
    int32_t renameIndex = -1;
    for (size_t i = 0; i < m_Visible.size(); i++)
    {
      const MaterialHandle handle = m_Entries[m_Visible[i]].handle;
      if (handle == m_ScrollTarget)
        scrollIndex = int32_t(i);
      if (handle == m_RenamingMaterial)
        renameIndex = int32_t(i);
    }

    if (scrollIndex < 0)
      m_ScrollTarget = MaterialHandle::Invalid();
    // Filtered out or deleted: the field is gone, so nothing would ever commit it
    if (renameIndex < 0 && m_RenamingMaterial != MaterialHandle::Invalid())
      FinishRename(materials, false);

    if (ImGui::BeginChild("MaterialList"))
    {
      const float rowHeight = ImGui::GetFrameHeight();

      ImGuiListClipper clipper;
      clipper.Begin(int32_t(m_Visible.size()));
      if (scrollIndex >= 0)
        clipper.IncludeItemByIndex(scrollIndex);
      if (renameIndex >= 0)
        clipper.IncludeItemByIndex(renameIndex);

      while (clipper.Step())
      {
        for (int32_t i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
          DrawRow(context, materials, m_Entries[m_Visible[size_t(i)]], rowHeight);
      }

      DrawBackgroundInput(context, materials);
    }
    ImGui::EndChild();
  }

  void MaterialBrowserPanel::ApplyPendingDelete(EditorContext& context, MaterialManager& materials)
  {
    const MaterialHandle handle = m_PendingDelete;
    m_PendingDelete = MaterialHandle::Invalid();
    if (handle == MaterialHandle::Invalid() || !materials.Has(handle))
      return;

    if (context.selectedMaterial == handle)
      context.ClearMaterialSelection();
    if (m_RenamingMaterial == handle)
      FinishRename(materials, false);

    if (context.scene != nullptr)
    {
      auto view = context.scene->GetView<MaterialComponent>();
      for (auto entity : view)
      {
        auto& mc = context.scene->GetComponent<MaterialComponent>(entity);
        if (mc.asset == handle)
          context.scene->RemoveComponent<MaterialComponent>(entity);
      }
    }

    materials.Destroy(handle);
  }

  void MaterialBrowserPanel::OnRender(EditorContext& context)
  {
    if (!BeginPanel())
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

    MaterialManager& materials = context.assetManager->Materials();

    if (EditorWidgets::InlineButton("Create", {
      .icon = ICON_LC_CIRCLE_PLUS,
      .tooltip = "Adds a material with default values and starts renaming it" }))
    {
      CreateMaterial(context, materials);
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##Search", ICON_LC_SEARCH " Search", m_FilterText, sizeof(m_FilterText));

    RefreshEntries(materials);

    if (m_Visible.empty())
    {
      FinishRename(materials, false);
      m_ScrollTarget = MaterialHandle::Invalid();

      ImGui::Spacing();
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      if (m_Entries.empty())
        ImGui::TextWrapped("No materials yet. Create adds one.");
      else
        ImGui::TextWrapped("No materials match \"%s\"", m_FilterText);
      ImGui::PopStyleColor();

      DrawBackgroundInput(context, materials);
    }
    else
    {
      DrawList(context, materials);
    }

    ApplyPendingDelete(context, materials);

    ImGui::End();
  }
}
