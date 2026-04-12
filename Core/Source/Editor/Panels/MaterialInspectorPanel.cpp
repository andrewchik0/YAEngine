#include "Editor/Panels/MaterialInspectorPanel.h"

#include <imgui.h>

#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorIcons.h"
#include "Assets/AssetManager.h"
#include "Editor/Utils/EditorTextureCache.h"
#include "Editor/Utils/FileDialog.h"

namespace YAEngine
{
  static const nfdu8filteritem_t s_ImageFilters[] = {
    { "Image Files", "png,jpg,jpeg,tga,bmp,hdr" }
  };

  static bool DrawTextureSlot(const char* label, TextureHandle& handle, bool linear,
                              EditorContext& context)
  {
    bool changed = false;

    ImGui::PushID(label);
    ImGui::Text("%s", label);

    if (handle && context.textureCache)
    {
      VkDescriptorSet ds = context.textureCache->GetOrRegister(handle);
      if (ds != VK_NULL_HANDLE)
        ImGui::Image((void*)ds, ImVec2(64, 64));
      else
        ImGui::Button("None", ImVec2(64, 64));
    }
    else
    {
      ImGui::Button("None", ImVec2(64, 64));
    }

    ImGui::SameLine();
    ImGui::BeginGroup();

    if (ImGui::Button(ICON_FA_FOLDER_OPEN " Load"))
    {
      std::string path = FileDialog::OpenFile(s_ImageFilters, 1);
      if (!path.empty())
      {
        bool* alphaPtr = nullptr;
        handle = context.assetManager->Textures().Load(path, alphaPtr, linear);
        changed = true;
      }
    }

    if (handle)
    {
      ImGui::SameLine();
      if (ImGui::Button(ICON_FA_TRASH_CAN))
      {
        handle = TextureHandle::Invalid();
        changed = true;
      }
    }

    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
  }

  void MaterialInspectorPanel::OnRender(EditorContext& context)
  {
    if (!ImGui::Begin("Material Inspector"))
    {
      ImGui::End();
      return;
    }

    if (!context.selectedMaterial || !context.assetManager ||
        !context.assetManager->Materials().Has(context.selectedMaterial))
    {
      ImGui::TextDisabled("No material selected");
      ImGui::End();
      return;
    }

    Material& mat = context.assetManager->Materials().Get(context.selectedMaterial);

    bool changed = false;

    if (ImGui::CollapsingHeader(ICON_FA_SLIDERS " Properties", ImGuiTreeNodeFlags_DefaultOpen))
    {
      char nameBuf[256] = {};
      snprintf(nameBuf, sizeof(nameBuf), "%s", mat.name.c_str());
      if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
        mat.name = nameBuf;

      changed |= ImGui::ColorEdit3("Albedo", &mat.albedo.x);
      changed |= ImGui::SliderFloat("Roughness", &mat.roughness, 0.0f, 1.0f);
      changed |= ImGui::SliderFloat("Metallic", &mat.metallic, 0.0f, 1.0f);
      changed |= ImGui::SliderFloat("Specular", &mat.specular, 0.0f, 1.0f);
      changed |= ImGui::ColorEdit3("Emissivity", &mat.emissivity.x);
      changed |= ImGui::Checkbox("Has Alpha", &mat.hasAlpha);
      changed |= ImGui::Checkbox("Alpha Test", &mat.alphaTest);
      changed |= ImGui::Checkbox("Combined Textures", &mat.combinedTextures);
    }

    if (ImGui::CollapsingHeader(ICON_FA_IMAGE " Textures", ImGuiTreeNodeFlags_DefaultOpen))
    {
      changed |= DrawTextureSlot("Base Color",  mat.baseColorTexture,  false, context);
      changed |= DrawTextureSlot("Metallic",    mat.metallicTexture,   true,  context);
      changed |= DrawTextureSlot("Roughness",   mat.roughnessTexture,  true,  context);
      changed |= DrawTextureSlot("Specular",    mat.specularTexture,   true,  context);
      changed |= DrawTextureSlot("Emissive",    mat.emissiveTexture,   false, context);
      changed |= DrawTextureSlot("Normal",      mat.normalTexture,     true,  context);
      changed |= DrawTextureSlot("Height",      mat.heightTexture,     true,  context);
    }

    if (changed)
      mat.MarkChanged();

    ImGui::End();
  }
}
