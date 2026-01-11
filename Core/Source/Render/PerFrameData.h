#pragma once

#include "VulkanDescriptorSet.h"
#include "VulkanUniformBuffer.h"

namespace YAEngine
{
  class PerFrameData
  {
  public:

    struct __PerFrameUBO
    {
      glm::mat4 view;
      glm::mat4 proj;
      glm::vec3 cameraPosition;
      float time;
      glm::vec3 cameraDirection;
      float gamma;
      float exposure;
    } ubo;

    void Init(VkDevice device, VmaAllocator allocator, VkDescriptorPool pool, uint32_t maxFramesInFlight);
    void Destroy();

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
    VkDevice m_Device {};
  };
}
