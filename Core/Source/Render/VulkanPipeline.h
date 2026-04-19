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
    bool additiveBlend = false;
    bool doubleSided = false;
    uint32_t colorAttachmentCount = 1;
    VkCompareOp compareOp = VK_COMPARE_OP_LESS;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    bool depthBiasEnable = false;

    // Compact vertex attribute format string: each pair is type + component count.
    // Types: f = float, i = int, u = uint.
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

    void Init(VkDevice device, VkRenderPass renderPass, const PipelineCreateInfo& info, VkPipelineCache pipelineCache = VK_NULL_HANDLE);
    void Destroy();

    void Bind(VkCommandBuffer commandBuffer);
    void BindDescriptorSets(VkCommandBuffer commandBuffer, std::initializer_list<VkDescriptorSet> descriptorSets, uint32_t set);
    void PushConstants(VkCommandBuffer cmd, void* data);

    VkPipeline Get() const { return m_Pipeline; }

    VkPipelineLayout GetLayout()
    {
      return m_PipelineLayout;
    }

  private:
    VkPipeline m_Pipeline {};
    VkPipelineLayout m_PipelineLayout {};
    uint32_t m_PushConstantSize = 0;

    std::vector<VkVertexInputAttributeDescription> GetVertexInputAttributeDescriptions(
      std::string_view vertexInput,
      std::vector<VkVertexInputBindingDescription>& bindings);

    VkDevice m_Device {};
  };
}
