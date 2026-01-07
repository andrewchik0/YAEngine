#pragma once
#include "VulkanDescriptorSet.h"
#include "VulkanPipeline.h"

namespace YAEngine
{
  class SkyBox
  {
  public:

    void Init(VkDevice device, VkDescriptorPool descriptorPool, VkRenderPass renderPass, uint32_t maxFramesInFlight);
    void Draw(unsigned int currentFrame, ::YAEngine::VulkanCubicTexture* cube, struct ::VkCommandBuffer_T* commandBuffer, glm::mat4& camDir, glm::mat4& proj);
    void Destroy();

  private:

    VulkanPipeline m_Pipeline;
    std::vector<VulkanDescriptorSet> m_DescriptorSets;

    VkDescriptorPool m_DescriptorPool {};
    VkDevice m_Device {};
  };
}
