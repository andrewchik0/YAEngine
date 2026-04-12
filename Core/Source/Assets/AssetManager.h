#pragma once

#include "Pch.h"
#include "IAssetManager.h"
#include "CubeMapManager.h"
#include "MaterialManager.h"
#include "MeshManager.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "Utils/PrimitiveMeshFactory.h"

namespace YAEngine
{
  struct RenderContext;
  struct CubicTextureResources;
  class VulkanTexture;

  class AssetManager
  {
  public:

    AssetManager();

    void Init(Scene& scene, std::function<uint32_t(uint32_t)>&& allocateInstanceData);
    void SetRenderContext(const RenderContext& ctx, const VulkanTexture& noneTexture, CubicTextureResources& cubicResources);

    template<typename T>
    T& GetManager()
    {
      auto it = m_Registry.find(std::type_index(typeid(T)));
      assert(it != m_Registry.end() && "Manager not registered");
      return static_cast<T&>(*it->second);
    }

    MeshManager& Meshes() { return GetManager<MeshManager>(); }
    TextureManager& Textures() { return GetManager<TextureManager>(); }
    MaterialManager& Materials() { return GetManager<MaterialManager>(); }
    ModelManager& Models() { return GetManager<ModelManager>(); }
    CubeMapManager& CubeMaps() { return GetManager<CubeMapManager>(); }
    PrimitiveMeshFactory& Primitives() { return m_PrimitiveFactory; }

    void SetBasePath(const std::string& basePath) { m_BasePath = basePath; }
    const std::string& GetBasePath() const { return m_BasePath; }
    std::string ResolvePath(const std::string& relativePath) const;
    std::string MakeRelative(const std::string& absolutePath) const;

    MaterialHandle FindOrCreateDefaultMaterial();

    void DestroyAll();

  private:

    template<typename T, typename... Args>
    void Register(Args&&... args)
    {
      auto manager = std::make_unique<T>(std::forward<Args>(args)...);
      auto* ptr = manager.get();
      m_Registry[std::type_index(typeid(T))] = std::move(manager);
      m_DestroyOrder.push_back(ptr);
    }

    std::unordered_map<std::type_index, std::unique_ptr<IAssetManager>> m_Registry;
    std::vector<IAssetManager*> m_DestroyOrder;
    PrimitiveMeshFactory m_PrimitiveFactory;
    std::string m_BasePath;
  };
}
