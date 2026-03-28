#include "AssetManager.h"

namespace YAEngine
{
  AssetManager::AssetManager()
  {
    Register<TextureManager>();
    Register<MeshManager>();
    Register<MaterialManager>();
    Register<CubeMapManager>();
    Register<ModelManager>();
  }

  void AssetManager::Init(Scene& scene, std::function<uint32_t(uint32_t)> allocateInstanceData)
  {
    Models().SetDependencies(&scene, this, std::move(allocateInstanceData));
  }

  void AssetManager::SetRenderContext(const RenderContext& ctx, const VulkanTexture& noneTexture, CubicTextureResources& cubicResources)
  {
    AssetManagerInitInfo info {
      .ctx = &ctx,
      .noneTexture = &noneTexture,
      .cubicResources = &cubicResources
    };

    for (auto& [typeIdx, manager] : m_Registry)
    {
      manager->SetRenderContext(info);
    }
  }

  void AssetManager::DestroyAll()
  {
    for (auto it = m_DestroyOrder.rbegin(); it != m_DestroyOrder.rend(); ++it)
    {
      (*it)->DestroyAll();
    }
  }
}
