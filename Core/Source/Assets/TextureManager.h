#pragma once
#include "AssetManagerBase.h"
#include "Render/VulkanTexture.h"

namespace YAEngine
{

  struct Texture
  {
  private:
    VulkanTexture m_VulkanTexture;

    friend class TextureManager;
    friend class Render;
    friend class VulkanMaterial;
  };

  using TextureHandle = AssetHandle;

  class TextureManager : public AssetManagerBase<Texture>
  {
  public:

    [[nodiscard]]
    TextureHandle Load(const std::string& filePath);
    void Destroy(TextureHandle handle);

    void DestroyAll();

  private:
  };
}
