#pragma once
#include "AssetManagerBase.h"
#include "IAssetManager.h"
#include "CpuResourceData.h"
#include "Render/VulkanTexture.h"

namespace YAEngine
{
  struct RenderContext;

  struct Texture
  {
  private:
    VulkanTexture m_VulkanTexture;
    bool m_HasAlpha = false;

    friend class TextureManager;
  };

  class TextureManager : public AssetManagerBase<Texture, TextureTag>, public IAssetManager
  {
  public:

    void SetRenderContext(const AssetManagerInitInfo& info) override { m_Ctx = info.ctx; }

    [[nodiscard]]
    TextureHandle Load(const std::string& filePath, bool* hasAlpha = nullptr, bool linear = false);

    static CpuTextureData DecodeToCpu(const std::string& filePath, bool linear = false);

    [[nodiscard]]
    TextureHandle LoadFromCpuData(CpuTextureData&& cpuData, bool* hasAlpha = nullptr, const std::string& cachePath = "");

    void Destroy(TextureHandle handle);

    static bool CheckAlpha(void* data, uint32_t width, uint32_t height);

    void DestroyAll() override;

    const VulkanTexture& GetVulkanTexture(TextureHandle handle)
    {
      return Get(handle).m_VulkanTexture;
    }

    struct PathInfo
    {
      std::string path;
      bool linear = false;
    };

    PathInfo GetPath(TextureHandle handle) const
    {
      for (auto& [key, cached] : m_Cache)
      {
        if (cached == handle)
          return { key.path, key.linear };
      }
      return {};
    }

  private:
    const RenderContext* m_Ctx = nullptr;

    struct CacheKey
    {
      std::string path;
      bool linear;

      bool operator==(const CacheKey&) const = default;
    };

    struct CacheKeyHash
    {
      size_t operator()(const CacheKey& k) const
      {
        size_t h = std::hash<std::string>()(k.path);
        h ^= std::hash<bool>()(k.linear) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
      }
    };

    std::unordered_map<CacheKey, TextureHandle, CacheKeyHash> m_Cache;
  };
}
