#pragma once

#include "Pch.h"
#include "VulkanBuffer.h"

namespace YAEngine
{
  struct RenderContext;

  // One triangles hit group. The closest hit shader is what fills the payload, and may be left
  // empty for rays that skip it; the any hit shader is optional. It runs for instances the TLAS
  // left non-opaque (alpha-tested ones, and glass under Glass Traversal Legacy), and for every
  // instance a trace with gl_RayFlagsNoOpaqueEXT reaches, FORCE_OPAQUE glass included.
  struct RaytracingHitGroup
  {
    std::string closestHitShaderFile;
    std::string anyHitShaderFile;
  };

  struct RaytracingPipelineCreateInfo
  {
    std::string raygenShaderFile;
    std::vector<std::string> missShaderFiles;
    std::vector<RaytracingHitGroup> hitGroups;
    std::vector<VkDescriptorSetLayout> sets;
    uint32_t pushConstantSize = 0;
    // Which stages may read the push constant block. Only consulted when the size is
    // non-zero, and a range has to name every stage that declares the block.
    VkShaderStageFlags pushConstantStages = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
  };

  // A VK_KHR_ray_tracing_pipeline pipeline together with the shader binding table that
  // addresses its groups. The two share a lifetime: group handles are only meaningful for
  // the pipeline they were fetched from, so recreating one always rebuilds the other.
  //
  // The context pointer is kept rather than the raw device and allocator, because the SBT
  // is a VMA allocation and PipelineCache's teardown and hot-reload paths are handed a
  // VkDevice alone. It points at RenderBackend's context, which outlives the cache.
  class VulkanRaytracingPipeline
  {
  public:

    void Init(const RenderContext& ctx, const RaytracingPipelineCreateInfo& info,
      VkPipelineCache vkCache = VK_NULL_HANDLE);
    void Destroy();

    void Bind(VkCommandBuffer cmd);
    void BindDescriptorSets(VkCommandBuffer cmd,
      std::initializer_list<VkDescriptorSet> descriptorSets, uint32_t set);
    void PushConstants(VkCommandBuffer cmd, const void* data);
    void TraceRays(VkCommandBuffer cmd, uint32_t width, uint32_t height, uint32_t depth = 1);

    VkPipeline Get() const { return m_Pipeline; }
    VkPipelineLayout GetLayout() const { return m_PipelineLayout; }

    bool IsValid() const { return m_Pipeline != VK_NULL_HANDLE; }

  private:

    // Fetches the group handles and lays them out in one buffer. groupCount counts the
    // raygen group, every miss group and every hit group, in that order - which is the
    // order Init built them in and the order the regions below index.
    void BuildShaderBindingTable(const RenderContext& ctx, uint32_t missCount, uint32_t hitCount);

    const RenderContext* m_Ctx {};
    VkPipeline m_Pipeline {};
    VkPipelineLayout m_PipelineLayout {};
    uint32_t m_PushConstantSize = 0;
    VkShaderStageFlags m_PushConstantStages = 0;

    // Raygen, miss and hit records all live in this one allocation, each region starting at
    // a shaderGroupBaseAlignment boundary inside it.
    VulkanBuffer m_ShaderBindingTable;
    VkStridedDeviceAddressRegionKHR m_RaygenRegion {};
    VkStridedDeviceAddressRegionKHR m_MissRegion {};
    VkStridedDeviceAddressRegionKHR m_HitRegion {};
    // Nothing calls a callable shader yet, and vkCmdTraceRaysKHR takes a zeroed region for
    // "there are none" rather than a null pointer.
    VkStridedDeviceAddressRegionKHR m_CallableRegion {};
  };
}
