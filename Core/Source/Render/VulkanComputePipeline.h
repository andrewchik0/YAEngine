#pragma once

#include "Pch.h"

namespace YAEngine
{

  class VulkanComputePipeline
  {
  public:

    void Init(VkDevice device, const std::string& shaderFile,
      const std::vector<VkDescriptorSetLayout>& sets,
      uint32_t pushConstantSize = 0,
      VkPipelineCache pipelineCache = VK_NULL_HANDLE);
    void Destroy();

    void Bind(VkCommandBuffer cmd);
    void BindDescriptorSets(VkCommandBuffer cmd, const std::vector<VkDescriptorSet>& descriptorSets, uint32_t set);
    void PushConstants(VkCommandBuffer cmd, const void* data);
    void Dispatch(VkCommandBuffer cmd, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);

    VkPipeline Get() const { return m_Pipeline; }
    VkPipelineLayout GetLayout() const { return m_PipelineLayout; }

  private:

    VkDevice m_Device {};
    VkPipeline m_Pipeline {};
    VkPipelineLayout m_PipelineLayout {};
    uint32_t m_PushConstantSize = 0;
  };
}
