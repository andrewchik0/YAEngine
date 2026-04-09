#pragma once

#include "RenderGraph.h"
#include "FrameUniformBuffer.h"
#include "TileLightBuffer.h"
#include "VulkanDescriptorSet.h"

namespace YAEngine
{
  class Render;
  struct FrameContext;
  struct CameraData;

  class OffscreenRenderer
  {
  public:

    void Init(Render& render, uint32_t resolution);
    void Destroy();

    // Render scene from given camera into internal LitColor image.
    // Returns reference to the LitColor VulkanImage in SHADER_READ_ONLY layout.
    VulkanImage& RenderFace(FrameContext& frame, const glm::vec3& position,
      const glm::mat4& faceView, uint32_t resolution);

    VulkanImage& GetLitColorImage() { return m_Graph.GetResource(m_LitColor); }

  private:

    void SetupGraph(uint32_t resolution);
    void InitDescriptors();

    Render* m_Render = nullptr;
    const RenderContext* m_Ctx = nullptr;

    RenderGraph m_Graph;
    FrameUniformBuffer m_FrameUBO;
    TileLightBuffer m_TileLightBuffer;

    // Graph resource handles
    RGHandle m_GBuffer0 {};
    RGHandle m_GBuffer1 {};
    RGHandle m_MainDepth {};
    RGHandle m_MainVelocity {};
    RGHandle m_LitColor {};

    // Pass indices
    uint32_t m_DepthPrepassIndex {};
    uint32_t m_GBufferPassIndex {};
    uint32_t m_LightCullPassIndex {};
    uint32_t m_DeferredLightingPassIndex {};

    // Descriptor sets for deferred lighting GBuffer input (set 1)
    VulkanDescriptorSet m_DeferredGBufferDescriptorSet;

    // Light cull input descriptor set (set 1: lights SSBO + depth sampler)
    VulkanDescriptorSet m_LightCullInputDescriptorSet;

    // Deferred lighting set 2 (lights + tile + shadow UBO + shadow atlas)
    VulkanDescriptorSet m_DeferredLightDescriptorSet;

    uint32_t m_Resolution = 0;
  };
}
