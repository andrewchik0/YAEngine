#include "MaterialManager.h"

namespace YAEngine
{
  MaterialHandle MaterialManager::Create()
  {
    auto material = std::make_unique<Material>();
    material->m_VulkanMaterial.Init();
    return AssetManagerBase::Load(std::move(material));
  }

  void MaterialManager::DestroyAll()
  {
    for (auto& mat : GetAll())
    {
      mat.second->m_VulkanMaterial.Destroy();
    }
    GetAll().clear();
  }
}
