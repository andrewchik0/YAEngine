#pragma once

#include "VulkanDescriptorSet.h"
#include "VulkanStorageBuffer.h"
#include "LightData.h"

namespace YAEngine
{
  struct RenderContext;

  class LightStorageBuffer
  {
  public:

    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    void SetUp(uint32_t frameIndex, const LightBuffer& data);

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
    std::vector<VulkanStorageBuffer> m_StorageBuffers;
  };
}
