#include "Editor/Panels/MaterialInspectorPanel.h"

#include <imgui.h>

#include "Editor/EditorContext.h"
#include "Assets/AssetManager.h"
#include "Editor/Utils/EditorTextureCache.h"
#include "Editor/Utils/FileDialog.h"

namespace YAEngine
{
  static const nfdu8filteritem_t s_ImageFilters[] = {
    { "Image Files", "png,jpg,jpeg,tga,bmp,hdr" }
  };

  static void DrawTextureSlot(const char* label, TextureHandle& handle, bool linear,
                              EditorContext& context)
  {
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

    if (ImGui::Button("Load"))
    {
      std::string path = FileDialog::OpenFile(s_ImageFilters, 1);
      if (!path.empty())
      {
        bool* alphaPtr = nullptr;
        handle = context.assetManager->Textures().Load(path, alphaPtr, linear);
      }
    }

    if (handle)
    {
      ImGui::SameLine();
      if (ImGui::Button("x"))
        handle = TextureHandle::Invalid();
    }

    ImGui::EndGroup();
    ImGui::PopID();
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

    if (ImGui::CollapsingHeader("Properties", ImGuiTreeNodeFlags_DefaultOpen))
    {
      char nameBuf[256] = {};
      snprintf(nameBuf, sizeof(nameBuf), "%s", mat.name.c_str());
      if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
        mat.name = nameBuf;

      ImGui::ColorEdit3("Albedo", &mat.albedo.x);
      ImGui::SliderFloat("Roughness", &mat.roughness, 0.0f, 1.0f);
      ImGui::SliderFloat("Metallic", &mat.metallic, 0.0f, 1.0f);
      ImGui::SliderFloat("Specular", &mat.specular, 0.0f, 1.0f);
      ImGui::ColorEdit3("Emissivity", &mat.emissivity.x);
      ImGui::Checkbox("Has Alpha", &mat.hasAlpha);
      ImGui::Checkbox("Combined Textures", &mat.combinedTextures);
    }

    if (ImGui::CollapsingHeader("Textures", ImGuiTreeNodeFlags_DefaultOpen))
    {
      DrawTextureSlot("Base Color",  mat.baseColorTexture,  false, context);
      DrawTextureSlot("Metallic",    mat.metallicTexture,   true,  context);
      DrawTextureSlot("Roughness",   mat.roughnessTexture,  true,  context);
      DrawTextureSlot("Specular",    mat.specularTexture,   true,  context);
      DrawTextureSlot("Emissive",    mat.emissiveTexture,   false, context);
      DrawTextureSlot("Normal",      mat.normalTexture,     true,  context);
      DrawTextureSlot("Height",      mat.heightTexture,     true,  context);
    }

    ImGui::End();
  }
}
