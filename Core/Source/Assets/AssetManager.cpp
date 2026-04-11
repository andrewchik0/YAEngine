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
    m_PrimitiveFactory.SetDependencies(&Meshes());
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

  std::string AssetManager::ResolvePath(const std::string& relativePath) const
  {
    if (m_BasePath.empty())
      return relativePath;

    bool isAbsolute = (relativePath.size() >= 2 && relativePath[1] == ':')
                   || (!relativePath.empty() && relativePath[0] == '/');
    if (isAbsolute)
      return relativePath;

    return m_BasePath + "/" + relativePath;
  }

  std::string AssetManager::MakeRelative(const std::string& absolutePath) const
  {
    if (m_BasePath.empty())
      return absolutePath;

    std::string base = m_BasePath;
    std::string path = absolutePath;
    std::replace(base.begin(), base.end(), '\\', '/');
    std::replace(path.begin(), path.end(), '\\', '/');

    if (!base.empty() && base.back() != '/')
      base += '/';

    if (path.starts_with(base))
      return path.substr(base.size());

    return absolutePath;
  }

  MaterialHandle AssetManager::FindOrCreateDefaultMaterial()
  {
    MaterialHandle found = MaterialHandle::Invalid();
    Materials().ForEachWithHandle([&](MaterialHandle handle, Material& mat)
    {
      if (!found && mat.name == "Default")
        found = handle;
    });
    if (found)
      return found;

    auto handle = Materials().Create();
    Materials().Get(handle).name = "Default";
    return handle;
  }

  void AssetManager::DestroyAll()
  {
    for (auto it = m_DestroyOrder.rbegin(); it != m_DestroyOrder.rend(); ++it)
    {
      (*it)->DestroyAll();
    }
    m_PrimitiveFactory.ClearCache();
  }
}
