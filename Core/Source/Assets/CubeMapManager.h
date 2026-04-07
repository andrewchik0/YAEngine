#pragma once
#include "AssetManagerBase.h"
#include "IAssetManager.h"
#include "Render/VulkanCubicTexture.h"

namespace YAEngine
{
  struct RenderContext;
  struct CubicTextureResources;

  struct CubeMap
  {
  private:
    VulkanCubicTexture m_CubeTexture;

    friend class CubeMapManager;
  };

  class CubeMapManager : public AssetManagerBase<CubeMap, CubeMapTag>, public IAssetManager
  {
  public:

    void SetRenderContext(const AssetManagerInitInfo& info) override
    {
      m_Ctx = info.ctx;
      m_CubicRes = info.cubicResources;
    }

    [[nodiscard]]
    CubeMapHandle Load(const std::string& filePath);
    void Destroy(CubeMapHandle handle);

    void DestroyAll() override;

    VulkanCubicTexture& GetVulkanCubicTexture(CubeMapHandle handle)
    {
      return Get(handle).m_CubeTexture;
    }

    std::string GetPath(CubeMapHandle handle) const
    {
      for (auto& [path, cached] : m_Cache)
      {
        if (cached == handle)
          return path;
      }
      return {};
    }

  private:
    const RenderContext* m_Ctx = nullptr;
    CubicTextureResources* m_CubicRes = nullptr;
    std::unordered_map<std::string, CubeMapHandle> m_Cache;
  };
}
