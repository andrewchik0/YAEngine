#pragma once

#include "VulkanImage.h"
#include "OffscreenRenderer.h"

namespace YAEngine
{
  struct RenderContext;
  struct FrameContext;
  struct CubicTextureResources;
  class LightProbeAtlas;

  class LightProbeBaker
  {
  public:

    void Init(Render& render, uint32_t resolution);
    void Destroy();

    void Bake(CubicTextureResources& cubicRes,
      FrameContext& frame, LightProbeAtlas& atlas,
      glm::vec3 position, uint32_t resolution, uint32_t atlasSlot,
      const std::string& irradianceSavePath = "",
      const std::string& prefilterSavePath = "");

  private:

    const RenderContext* m_Ctx = nullptr;
    OffscreenRenderer m_OffscreenRenderer;
  };
}
