#pragma once

#include "RenderGraph.h"
#include "FrameUniformBuffer.h"

namespace YAEngine
{
  class Render;
  struct FrameContext;

  // Renders the backface mask that decides whether an irradiance volume node is
  // buried behind geometry instead of standing in open space.
  //
  // Deliberately not a mode of OffscreenRenderer: the mask needs no GBuffer, no
  // light culling and no lighting, and it runs for EVERY node - including the ones
  // that are then rejected and never get a lighting capture at all. Sharing the
  // full capture graph would make the classification cost more than the bake it is
  // supposed to shorten.
  //
  // The pass renders with VK_CULL_MODE_NONE and depth testing, so each texel ends
  // up holding the winding of the NEAREST surface in that direction. That is the
  // only signal the classification needs, and it is unavailable from the ordinary
  // capture: with back faces culled, a node standing behind a wall does not see the
  // wall at all and records open sky instead.
  class BackfaceRatioSampler
  {
  public:

    void Init(Render& render, uint32_t resolution);
    void Destroy();

    // Renders one cube face of the mask. The returned image is in
    // COLOR_ATTACHMENT_OPTIMAL and is overwritten by the next call, so the caller
    // must copy it out before asking for another face.
    VulkanImage& RenderFace(FrameContext& frame, const glm::vec3& position,
      const glm::mat4& faceView);

    uint32_t GetResolution() const { return m_Resolution; }

  private:

    void SetupGraph(uint32_t resolution);

    Render* m_Render = nullptr;
    const RenderContext* m_Ctx = nullptr;

    RenderGraph m_Graph;
    FrameUniformBuffer m_FrameUBO;

    RGHandle m_Depth {};
    RGHandle m_Mask {};
    uint32_t m_MaskPassIndex {};

    uint32_t m_Resolution = 0;
  };
}
