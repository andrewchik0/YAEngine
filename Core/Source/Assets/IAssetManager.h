#pragma once

namespace YAEngine
{
  struct RenderContext;
  class VulkanTexture;
  struct CubicTextureResources;

  struct AssetManagerInitInfo
  {
    const RenderContext* ctx = nullptr;
    const VulkanTexture* noneTexture = nullptr;
    CubicTextureResources* cubicResources = nullptr;
  };

  class IAssetManager
  {
  public:
    virtual ~IAssetManager() = default;
    virtual void SetRenderContext(const AssetManagerInitInfo& info) = 0;
    virtual void DestroyAll() = 0;
  };
}
