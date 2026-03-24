#include "VulkanComputePipeline.h"

#include <fstream>
#include <ios>

#include "Log.h"

namespace YAEngine
{

  void VulkanComputePipeline::Init(VkDevice device, const std::string& shaderFile,
    const std::vector<VkDescriptorSetLayout>& sets,
    uint32_t pushConstantSize,
    VkPipelineCache pipelineCache)
  {
    m_Device = device;
    m_PushConstantSize = pushConstantSize;

    auto code = ReadFile(shaderFile);

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = code.size();
    moduleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_Device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create compute shader module: %s", shaderFile.c_str());
      throw std::runtime_error("Failed to create compute shader module!");
    }

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

  void VulkanComputePipeline::BindDescriptorSets(VkCommandBuffer cmd, const std::vector<VkDescriptorSet>& descriptorSets, uint32_t set)
  {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
      m_PipelineLayout, set,
      static_cast<uint32_t>(descriptorSets.size()),
      descriptorSets.data(), 0, nullptr);
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

  std::vector<char> VulkanComputePipeline::ReadFile(std::string_view filename)
  {
    std::string filepath = SHADER_BIN_DIR;
    filepath += '/';
    filepath += filename;
    filepath += ".spv";

    std::ifstream file(filepath, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
      YA_LOG_ERROR("Render", "Failed to open shader file: %s", filepath.c_str());
      throw std::runtime_error("Failed to open shader file!");
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
  }
}
