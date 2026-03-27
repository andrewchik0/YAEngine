#pragma once

#include "VulkanDescriptorSet.h"
#include "VulkanUniformBuffer.h"
#include "FrameUniforms.h"

namespace YAEngine
{
  struct RenderContext;

  class FrameUniformBuffer
  {
  public:

    FrameUniforms uniforms;

    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    void SetUp(uint32_t frameIndex);

    VkDescriptorSetLayout GetLayout()
    {
      return m_DescriptorSets[0].GetLayout();
    }

    VkDescriptorSet GetDescriptorSet(uint32_t frameIndex)
    {
      return m_DescriptorSets[frameIndex].Get();
    }

  private:

    std::vector<VulkanDescriptorSet> m_DescriptorSets;
    std::vector<VulkanUniformBuffer> m_UniformBuffers;
  };
}
