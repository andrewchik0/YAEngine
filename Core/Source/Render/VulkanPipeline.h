#pragma once

#include "Pch.h"

namespace YAEngine
{

  struct PipelineCreateInfo
  {
    std::string fragmentShaderFile;
    std::string vertexShaderFile;
    std::string geometryShaderFile;

    uint32_t pushConstantSize = sizeof(glm::mat4);

    bool depthTesting = true;
    bool depthWrite = true;
    bool blending = false;
    bool doubleSided = false;
    VkCompareOp compareOp = VK_COMPARE_OP_LESS;

    // "f2i3u4"
    //    |
    //    V
    // (layout = 0) vec2
    // (layout = 1) ivec3
    // (layout = 2) uvec4
    std::string vertexInputFormat;

    std::vector<VkDescriptorSetLayout> sets;
  };

  class VulkanPipeline
  {
  public:

    void Init(VkDevice device, VkRenderPass renderPass, const PipelineCreateInfo& info);
    void Destroy();

    void Bind(VkCommandBuffer commandBuffer);
    void BindDescriptorSets(VkCommandBuffer commandBuffer, const std::vector<VkDescriptorSet>& descriptorSets, uint32_t set);
    void PushConstants(VkCommandBuffer cmd, void* data);

    VkPipelineLayout GetLayout()
    {
      return m_PipelineLayout;
    }

  private:
    VkPipeline m_Pipeline {};
    VkPipelineLayout m_PipelineLayout {};
    uint32_t m_PushConstantSize = 0;

    static std::vector<char> ReadFile(std::string_view filename);
    static VkShaderModule CreateShaderModule(VkDevice device, const std::vector<char>& code);

    std::vector<VkVertexInputAttributeDescription> GetVertexInputAttributeDescriptions(std::string_view vertexInput, uint32_t* vertexSize);

    friend class VulkanCubicTexture;

    VkDevice m_Device {};
  };
}