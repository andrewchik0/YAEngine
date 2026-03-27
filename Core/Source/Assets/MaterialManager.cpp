#include "MaterialManager.h"

#include "Render/RenderContext.h"

namespace YAEngine
{
  MaterialHandle MaterialManager::Create()
  {
    auto material = std::make_unique<Material>();
    material->name = "Material " + std::to_string(m_NextId++);
    material->m_VulkanMaterial.Init(*m_Ctx, *m_NoneTexture);
    return Store(std::move(material));
  }

  MaterialHandle MaterialManager::Duplicate(MaterialHandle source)
  {
    Material& src = Get(source);
    auto handle = Create();
    Material& dst = Get(handle);

    dst.name = src.name + " (Copy)";
    dst.albedo = src.albedo;
    dst.emissivity = src.emissivity;
    dst.roughness = src.roughness;
    dst.metallic = src.metallic;
    dst.specular = src.specular;
    dst.sg = src.sg;
    dst.hasAlpha = src.hasAlpha;
    dst.combinedTextures = src.combinedTextures;
    dst.baseColorTexture = src.baseColorTexture;
    dst.metallicTexture = src.metallicTexture;
    dst.roughnessTexture = src.roughnessTexture;
    dst.specularTexture = src.specularTexture;
    dst.emissiveTexture = src.emissiveTexture;
    dst.normalTexture = src.normalTexture;
    dst.heightTexture = src.heightTexture;
    dst.cubemap = src.cubemap;

    return handle;
  }

  void MaterialManager::Destroy(MaterialHandle handle)
  {
    Get(handle).m_VulkanMaterial.Destroy(*m_Ctx);
    Remove(handle);
  }

  void MaterialManager::DestroyAll()
  {
    ForEach([this](Material& mat) {
      mat.m_VulkanMaterial.Destroy(*m_Ctx);
    });
    Clear();
  }
}
