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

  void AssetManager::Init(Scene& scene, std::function<uint32_t(uint32_t)>&& allocateInstanceData)
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

    std::string result;
    result.reserve(m_BasePath.size() + 1 + relativePath.size());
    result.append(m_BasePath);
    result.push_back('/');
    result.append(relativePath);
    return result;
  }

  std::string AssetManager::MakeRelative(const std::string& absolutePath) const
  {
    if (m_BasePath.empty())
      return absolutePath;

    // Compare paths treating both '/' and '\\' as separators
    auto normalize = [](char c) { return c == '\\' ? '/' : c; };

    std::string_view base = m_BasePath;
    std::string_view path = absolutePath;

    // Check if path starts with base (case-sensitive, slash-normalized)
    size_t baseLen = base.size();
    if (path.size() < baseLen)
      return absolutePath;

    for (size_t i = 0; i < baseLen; ++i)
    {
      if (normalize(path[i]) != normalize(base[i]))
        return absolutePath;
    }

    // Skip trailing separator after base
    size_t offset = baseLen;
    if (offset < path.size() && (path[offset] == '/' || path[offset] == '\\'))
      ++offset;

    return std::string(path.substr(offset));
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
