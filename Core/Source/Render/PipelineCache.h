#pragma once

#include "VulkanPipeline.h"
#include "VulkanComputePipeline.h"

namespace YAEngine
{

  struct GraphicsPipelineKey
  {
    std::string vertexShaderFile;
    std::string fragmentShaderFile;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    bool depthTesting = true;
    bool depthWrite = true;
    bool blending = false;
    bool doubleSided = false;
    VkCompareOp compareOp = VK_COMPARE_OP_LESS;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool depthBiasEnable = false;
    uint32_t colorAttachmentCount = 1;
    uint32_t pushConstantSize = 0;
    std::string vertexInputFormat;
    std::vector<VkDescriptorSetLayout> sets;

    bool operator==(const GraphicsPipelineKey& other) const;
  };

  struct GraphicsPipelineKeyHash
  {
    size_t operator()(const GraphicsPipelineKey& key) const;
  };

  struct ComputePipelineKey
  {
    std::string shaderFile;
    uint32_t pushConstantSize = 0;
    std::vector<VkDescriptorSetLayout> sets;

    bool operator==(const ComputePipelineKey& other) const;
  };

  struct ComputePipelineKeyHash
  {
    size_t operator()(const ComputePipelineKey& key) const;
  };

  struct PipelineHandle
  {
    uint32_t index = UINT32_MAX;
    explicit operator bool() const { return index != UINT32_MAX; }
  };

  class PipelineCache
  {
  public:

    PipelineHandle Register(
      VkDevice device,
      VkRenderPass renderPass,
      const PipelineCreateInfo& info,
      VkPipelineCache vkCache = VK_NULL_HANDLE);

    PipelineHandle RegisterCompute(
      VkDevice device,
      const std::string& shaderFile,
      const std::vector<VkDescriptorSetLayout>& sets,
      uint32_t pushConstantSize = 0,
      VkPipelineCache vkCache = VK_NULL_HANDLE);

    VulkanPipeline& Get(PipelineHandle handle);
    VulkanComputePipeline& GetCompute(PipelineHandle handle);

    void Destroy();

#ifdef YA_EDITOR
    void RecreatePipelinesForShader(VkDevice device, const std::string& shaderFile);
#endif

  private:

    VulkanPipeline& GetOrCreate(
      VkDevice device,
      VkRenderPass renderPass,
      const PipelineCreateInfo& info,
      VkPipelineCache vkCache);

    VulkanComputePipeline& GetOrCreateCompute(
      VkDevice device,
      const std::string& shaderFile,
      const std::vector<VkDescriptorSetLayout>& sets,
      uint32_t pushConstantSize,
      VkPipelineCache vkCache);

    std::unordered_map<GraphicsPipelineKey, VulkanPipeline, GraphicsPipelineKeyHash> m_GraphicsCache;
    std::unordered_map<ComputePipelineKey, VulkanComputePipeline, ComputePipelineKeyHash> m_ComputeCache;

    std::vector<VulkanPipeline*> m_GraphicsPipelines;
    std::vector<VulkanComputePipeline*> m_ComputePipelines;

#ifdef YA_EDITOR
    struct GraphicsEntry
    {
      VulkanPipeline* pipeline;
      PipelineCreateInfo info;
      VkRenderPass renderPass;
      VkPipelineCache vkCache;
    };

    struct ComputeEntry
    {
      VulkanComputePipeline* pipeline;
      std::string shaderFile;
      std::vector<VkDescriptorSetLayout> sets;
      uint32_t pushConstantSize;
      VkPipelineCache vkCache;
    };

    std::vector<GraphicsEntry> m_GraphicsEntries;
    std::vector<ComputeEntry> m_ComputeEntries;
    std::unordered_map<std::string, std::vector<PipelineHandle>> m_ShaderToGraphics;
    std::unordered_map<std::string, std::vector<PipelineHandle>> m_ShaderToCompute;
#endif
  };

}
