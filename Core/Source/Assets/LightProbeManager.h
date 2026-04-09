#pragma once

#include "AssetManagerBase.h"
#include "IAssetManager.h"
#include "Render/VulkanCubicTexture.h"

namespace YAEngine
{
  struct RenderContext;
  struct CubicTextureResources;

  struct LightProbe
  {
    uint32_t resolution = 128;
    uint32_t generation = 0;
    bool baked = false;

  private:
    VulkanCubicTexture m_CubeTexture;

    friend class LightProbeManager;
  };

  class LightProbeManager : public AssetManagerBase<LightProbe, LightProbeTag>, public IAssetManager
  {
  public:

    void SetRenderContext(const AssetManagerInitInfo& info) override
    {
      m_Ctx = info.ctx;
      m_CubicRes = info.cubicResources;
    }

    [[nodiscard]]
    LightProbeHandle Create(uint32_t resolution = 128);
    void Destroy(LightProbeHandle handle);

    void DestroyAll() override;

    VulkanCubicTexture& GetCubicTexture(LightProbeHandle handle)
    {
      return Get(handle).m_CubeTexture;
    }

    bool IsBaked(LightProbeHandle handle)
    {
      return Has(handle) && Get(handle).baked;
    }

    void MarkBaked(LightProbeHandle handle)
    {
      Get(handle).baked = true;
      Get(handle).generation++;
    }

  private:
    const RenderContext* m_Ctx = nullptr;
    CubicTextureResources* m_CubicRes = nullptr;
  };
}
