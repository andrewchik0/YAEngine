#include "MaterialManager.h"

#include "Render/RenderContext.h"

namespace YAEngine
{
  MaterialHandle MaterialManager::Create()
  {
    auto material = std::make_unique<Material>();
    material->m_VulkanMaterial.Init(*m_Ctx, *m_NoneTexture);
    return AssetManagerBase::Load(std::move(material));
  }

  void MaterialManager::DestroyAll()
  {
    for (auto& mat : GetAll())
    {
      mat.second->m_VulkanMaterial.Destroy(*m_Ctx);
    }
    GetAll().clear();
  }
}
