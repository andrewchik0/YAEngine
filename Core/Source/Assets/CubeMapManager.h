#pragma once
#include "AssetManagerBase.h"
#include "Render/VulkanCubicTexture.h"

namespace YAEngine
{
  struct RenderContext;
  struct CubicTextureResources;

  struct CubeMap
  {
  private:
    VulkanCubicTexture m_CubeTexture;

    friend class Render;
    friend class CubeMapManager;
    friend class VulkanMaterial;
  };

  using CubeMapHandle = AssetHandle;

  class CubeMapManager : public AssetManagerBase<CubeMap>
  {
  public:

    void SetRenderContext(const RenderContext& ctx, CubicTextureResources& cubicRes)
    {
      m_Ctx = &ctx;
      m_CubicRes = &cubicRes;
    }

    [[nodiscard]]
    CubeMapHandle Load(const std::string& filePath);
    void Destroy(CubeMapHandle handle);

    void DestroyAll();

  private:
    const RenderContext* m_Ctx = nullptr;
    CubicTextureResources* m_CubicRes = nullptr;
  };
}
