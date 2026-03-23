#include "MaterialManager.h"

#include "Render/RenderContext.h"

namespace YAEngine
{
  MaterialHandle MaterialManager::Create()
  {
    auto material = std::make_unique<Material>();
    material->m_VulkanMaterial.Init(*m_Ctx, *m_NoneTexture);
    return Store(std::move(material));
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
