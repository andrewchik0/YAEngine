#pragma once

#include "VulkanImage.h"
#include "OffscreenRenderer.h"
#include "BakeLimits.h"

namespace YAEngine
{
  struct RenderContext;
  struct FrameContext;
  struct CubicTextureResources;
  class ReflectionProbeAtlas;

  class ReflectionProbeBaker
  {
  public:

    void Init(Render& render, uint32_t resolution);
    void Destroy();

    // Specular only - a probe no longer produces an irradiance cubemap, diffuse
    // indirect comes from irradiance volumes.
    void Bake(CubicTextureResources& cubicRes,
      FrameContext& frame, ReflectionProbeAtlas& atlas,
      glm::vec3 position, uint32_t resolution, uint32_t atlasSlot,
      const std::string& prefilterSavePath = "");

  private:

    // The offscreen graph is sized at Init, so a different capture resolution
    // means rebuilding it. Bakes are rare and already wait for the device.
    void EnsureResolution(uint32_t resolution);

    Render* m_Render = nullptr;
    const RenderContext* m_Ctx = nullptr;
    OffscreenRenderer m_OffscreenRenderer;
    uint32_t m_Resolution = 0;
  };
}
