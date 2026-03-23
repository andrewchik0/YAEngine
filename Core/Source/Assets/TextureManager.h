#pragma once
#include "AssetManagerBase.h"
#include "Render/VulkanTexture.h"

namespace YAEngine
{
  struct RenderContext;

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

    void SetRenderContext(const RenderContext& ctx) { m_Ctx = &ctx; }

    [[nodiscard]]
    TextureHandle Load(const std::string& filePath, bool* hasAlpha = nullptr, bool linear = false);
    void Destroy(TextureHandle handle);

    bool CheckAlpha(void* data, uint32_t width, uint32_t height);

    void DestroyAll();

  private:
    const RenderContext* m_Ctx = nullptr;
  };
}
