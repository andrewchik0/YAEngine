#include "VulkanComputePipeline.h"

#include "ShaderUtils.h"

namespace YAEngine
{

  void VulkanComputePipeline::Init(VkDevice device, const std::string& shaderFile,
    const std::vector<VkDescriptorSetLayout>& sets,
    uint32_t pushConstantSize,
    VkPipelineCache pipelineCache)
  {
    m_Device = device;
    m_PushConstantSize = pushConstantSize;

    auto code = ReadShaderFile(shaderFile);
    VkShaderModule shaderModule = CreateShaderModule(m_Device, code);

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<uint32_t>(sets.size());
    layoutInfo.pSetLayouts = sets.data();

    VkPushConstantRange range{};
    if (pushConstantSize > 0)
    {
      range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
      range.offset = 0;
      range.size = pushConstantSize;
      layoutInfo.pushConstantRangeCount = 1;
      layoutInfo.pPushConstantRanges = &range;
    }

    if (vkCreatePipelineLayout(m_Device, &layoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create compute pipeline layout");
      throw std::runtime_error("Failed to create compute pipeline layout!");
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = m_PipelineLayout;

    if (vkCreateComputePipelines(m_Device, pipelineCache, 1, &pipelineInfo, nullptr, &m_Pipeline) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create compute pipeline");
      throw std::runtime_error("Failed to create compute pipeline!");
    }

    vkDestroyShaderModule(m_Device, shaderModule, nullptr);
  }

  void VulkanComputePipeline::Destroy()
  {
    vkDestroyPipeline(m_Device, m_Pipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
  }

  void VulkanComputePipeline::Bind(VkCommandBuffer cmd)
  {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipeline);
  }

  void VulkanComputePipeline::BindDescriptorSets(VkCommandBuffer cmd, std::initializer_list<VkDescriptorSet> descriptorSets, uint32_t set)
  {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
      m_PipelineLayout, set,
      static_cast<uint32_t>(descriptorSets.size()),
      descriptorSets.begin(), 0, nullptr);
  }

  void VulkanComputePipeline::PushConstants(VkCommandBuffer cmd, const void* data)
  {
    vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
      0, m_PushConstantSize, data);
  }

  void VulkanComputePipeline::Dispatch(VkCommandBuffer cmd, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
  {
    vkCmdDispatch(cmd, groupCountX, groupCountY, groupCountZ);
  }
}
