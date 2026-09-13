#pragma once

#include "VulkanPipeline.h"
#include "VulkanComputePipeline.h"
#include "VulkanRaytracingPipeline.h"

namespace YAEngine
{
  struct RenderContext;

  struct GraphicsPipelineKey
  {
    std::string vertexShaderFile;
    std::string fragmentShaderFile;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    bool depthTesting = true;
    bool depthWrite = true;
    bool blending = false;
    bool additiveBlend = false;
    bool premultipliedAlpha = false;
    bool doubleSided = false;
    VkCompareOp compareOp = VK_COMPARE_OP_GREATER;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool depthBiasEnable = false;
    bool depthClampEnable = false;
    bool dynamicCullMode = false;
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

  // Every stage file of the pipeline flattened into one list, in the order Init consumes
  // them: raygen, the miss shaders, then each hit group's closest hit and any hit. Two
  // pipelines with the same stages in the same order and the same layout are one pipeline.
  struct RayTracingPipelineKey
  {
    std::vector<std::string> shaderFiles;
    uint32_t pushConstantSize = 0;
    std::vector<VkDescriptorSetLayout> sets;

    bool operator==(const RayTracingPipelineKey& other) const;
  };

  struct RayTracingPipelineKeyHash
  {
    size_t operator()(const RayTracingPipelineKey& key) const;
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

    // Takes the whole context rather than a device: the pipeline owns a shader binding
    // table, which is a VMA allocation, and it has to be rebuilt whenever the pipeline is -
    // group handles are only meaningful for the pipeline they came from.
    PipelineHandle RegisterRayTracing(
      const RenderContext& ctx,
      const RaytracingPipelineCreateInfo& info,
      VkPipelineCache vkCache = VK_NULL_HANDLE);

    VulkanPipeline& Get(PipelineHandle handle);
    VulkanComputePipeline& GetCompute(PipelineHandle handle);
    VulkanRaytracingPipeline& GetRayTracing(PipelineHandle handle);

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

    VulkanRaytracingPipeline& GetOrCreateRayTracing(
      const RenderContext& ctx,
      const RayTracingPipelineKey& key,
      const RaytracingPipelineCreateInfo& info,
      VkPipelineCache vkCache);

    // Flattens info's stage files in the order the pipeline consumes them.
    static RayTracingPipelineKey MakeRayTracingKey(const RaytracingPipelineCreateInfo& info);

    std::unordered_map<GraphicsPipelineKey, VulkanPipeline, GraphicsPipelineKeyHash> m_GraphicsCache;
    std::unordered_map<ComputePipelineKey, VulkanComputePipeline, ComputePipelineKeyHash> m_ComputeCache;
    std::unordered_map<RayTracingPipelineKey, VulkanRaytracingPipeline, RayTracingPipelineKeyHash> m_RayTracingCache;

    std::vector<VulkanPipeline*> m_GraphicsPipelines;
    std::vector<VulkanComputePipeline*> m_ComputePipelines;
    std::vector<VulkanRaytracingPipeline*> m_RayTracingPipelines;

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

    struct RayTracingEntry
    {
      VulkanRaytracingPipeline* pipeline;
      // The pipeline needs it again to rebuild its shader binding table, and the reload
      // path is handed a VkDevice alone. Points at RenderBackend's context.
      const RenderContext* ctx;
      RaytracingPipelineCreateInfo info;
      VkPipelineCache vkCache;
    };

    std::vector<GraphicsEntry> m_GraphicsEntries;
    std::vector<ComputeEntry> m_ComputeEntries;
    std::vector<RayTracingEntry> m_RayTracingEntries;
    std::unordered_map<std::string, std::vector<PipelineHandle>> m_ShaderToGraphics;
    std::unordered_map<std::string, std::vector<PipelineHandle>> m_ShaderToCompute;
    // Every stage file of a ray tracing pipeline maps to it, not only the raygen one: a
    // reload of any of rgen, miss, closest hit or any hit changes the module set the
    // pipeline was built from and invalidates its group handles with it.
    std::unordered_map<std::string, std::vector<PipelineHandle>> m_ShaderToRayTracing;
#endif
  };

}
