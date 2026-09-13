#include "VulkanRaytracingPipeline.h"

#include "RenderContext.h"
#include "ShaderUtils.h"

namespace YAEngine
{
  namespace
  {
    // Everything the shader binding table needs from its allocation: the table is addressed
    // by device address, the driver reads it as a binding table, and TRANSFER_DST leaves a
    // staged upload possible for a table too large to want host visible memory.
    constexpr VkBufferUsageFlags SHADER_BINDING_TABLE_USAGE =
      VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    // One. The recursion depth is what a traceRayEXT issued from a hit or miss shader would
    // need, and nothing here does that: the primary ray is traced from the ray generation
    // shader, and the path tracer's bounce loop lives there too, so every ray is depth one.
    // Shadow and visibility rays cost nothing extra for the same reason - they are either
    // ray queries, which do not count against this at all, or another rgen-level trace.
    // Raising it costs traversal stack the driver has to reserve per invocation.
    constexpr uint32_t MAX_RAY_RECURSION_DEPTH = 1;

    template<typename T>
    constexpr T AlignUp(T value, T alignment)
    {
      return alignment == 0 ? value : (value + alignment - 1) / alignment * alignment;
    }
  }

  void VulkanRaytracingPipeline::Init(const RenderContext& ctx,
    const RaytracingPipelineCreateInfo& info, VkPipelineCache vkCache)
  {
    m_Ctx = &ctx;
    m_PushConstantSize = info.pushConstantSize;
    m_PushConstantStages = info.pushConstantStages;

    if (!ctx.rayTracing.IsPipelineLoaded())
    {
      YA_LOG_ERROR("Vulkan", "Ray tracing pipeline '%s' cannot be created, the entry points are missing",
        info.raygenShaderFile.c_str());
      throw std::runtime_error("Ray tracing pipeline entry points are missing!");
    }

    std::vector<VkShaderModule> modules;
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;

    auto addStage = [&](const std::string& shaderFile, VkShaderStageFlagBits stage)
    {
      VkShaderModule shaderModule = CreateShaderModule(ctx.device, ReadShaderFile(shaderFile));
      modules.push_back(shaderModule);
      stages.push_back(VkPipelineShaderStageCreateInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = stage,
        .module = shaderModule,
        .pName = "main",
      });
      return static_cast<uint32_t>(stages.size() - 1);
    };

    auto addGeneralGroup = [&](uint32_t stageIndex)
    {
      groups.push_back(VkRayTracingShaderGroupCreateInfoKHR {
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
        .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
        .generalShader = stageIndex,
        .closestHitShader = VK_SHADER_UNUSED_KHR,
        .anyHitShader = VK_SHADER_UNUSED_KHR,
        .intersectionShader = VK_SHADER_UNUSED_KHR,
      });
    };

    // Group order is the shader binding table's own order: the raygen group first, then
    // every miss group, then every hit group. BuildShaderBindingTable indexes the handles
    // it fetches with exactly that assumption.
    addGeneralGroup(addStage(info.raygenShaderFile, VK_SHADER_STAGE_RAYGEN_BIT_KHR));

    for (const std::string& missShaderFile : info.missShaderFiles)
      addGeneralGroup(addStage(missShaderFile, VK_SHADER_STAGE_MISS_BIT_KHR));

    for (const RaytracingHitGroup& hitGroup : info.hitGroups)
    {
      const uint32_t closestHit = addStage(hitGroup.closestHitShaderFile,
        VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
      // A group without an any-hit shader is the normal case: the TLAS marks everything but
      // an alpha-tested instance FORCE_OPAQUE, so traversal would never call one.
      const uint32_t anyHit = hitGroup.anyHitShaderFile.empty()
        ? VK_SHADER_UNUSED_KHR
        : addStage(hitGroup.anyHitShaderFile, VK_SHADER_STAGE_ANY_HIT_BIT_KHR);

      groups.push_back(VkRayTracingShaderGroupCreateInfoKHR {
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
        .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
        .generalShader = VK_SHADER_UNUSED_KHR,
        .closestHitShader = closestHit,
        .anyHitShader = anyHit,
        .intersectionShader = VK_SHADER_UNUSED_KHR,
      });
    }

    VkPipelineLayoutCreateInfo layoutInfo {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<uint32_t>(info.sets.size());
    layoutInfo.pSetLayouts = info.sets.data();

    VkPushConstantRange range {};
    if (info.pushConstantSize > 0)
    {
      range.stageFlags = info.pushConstantStages;
      range.offset = 0;
      range.size = info.pushConstantSize;
      layoutInfo.pushConstantRangeCount = 1;
      layoutInfo.pPushConstantRanges = &range;
    }

    if (vkCreatePipelineLayout(ctx.device, &layoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create ray tracing pipeline layout for '%s'",
        info.raygenShaderFile.c_str());
      throw std::runtime_error("Failed to create ray tracing pipeline layout!");
    }

    VkRayTracingPipelineCreateInfoKHR pipelineInfo {
      .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
      .stageCount = static_cast<uint32_t>(stages.size()),
      .pStages = stages.data(),
      .groupCount = static_cast<uint32_t>(groups.size()),
      .pGroups = groups.data(),
      .maxPipelineRayRecursionDepth = MAX_RAY_RECURSION_DEPTH,
      .layout = m_PipelineLayout,
    };

    // No deferred host operation: the compile blocks, exactly as every other pipeline in
    // the engine does, and there is no thread here that would wait on it anyway.
    VkResult result = ctx.rayTracing.createRayTracingPipelines(ctx.device,
      VK_NULL_HANDLE, vkCache, 1, &pipelineInfo, nullptr, &m_Pipeline);

    for (VkShaderModule shaderModule : modules)
      vkDestroyShaderModule(ctx.device, shaderModule, nullptr);

    if (result != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create ray tracing pipeline '%s': %d",
        info.raygenShaderFile.c_str(), result);
      throw std::runtime_error("Failed to create ray tracing pipeline!");
    }

    BuildShaderBindingTable(ctx,
      static_cast<uint32_t>(info.missShaderFiles.size()),
      static_cast<uint32_t>(info.hitGroups.size()));
  }

  void VulkanRaytracingPipeline::BuildShaderBindingTable(const RenderContext& ctx,
    uint32_t missCount, uint32_t hitCount)
  {
    const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& properties = ctx.rayTracingPipelineProperties;

    const uint32_t handleSize = properties.shaderGroupHandleSize;
    // A handle is copied at its own size but stored at this stride, which is what the
    // stride of a miss or hit region has to be a multiple of.
    const uint32_t handleStride = AlignUp(handleSize, std::max(properties.shaderGroupHandleAlignment, 1u));
    // Every region's base address has to sit on this, which is coarser than the handle
    // alignment - hence the per-region padding below.
    const VkDeviceSize baseAlignment = std::max<VkDeviceSize>(properties.shaderGroupBaseAlignment, 1);
    const uint32_t groupCount = 1 + missCount + hitCount;

    // Fetched tightly packed at handleSize, whatever the strides above are.
    std::vector<uint8_t> handles(static_cast<size_t>(groupCount) * handleSize);
    VkResult result = ctx.rayTracing.getRayTracingShaderGroupHandles(ctx.device, m_Pipeline,
      0, groupCount, handles.size(), handles.data());
    if (result != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to fetch %u ray tracing shader group handle(s): %d",
        groupCount, result);
      throw std::runtime_error("Failed to fetch ray tracing shader group handles!");
    }

    // The raygen region is the odd one out: the specification requires its size to equal
    // its stride, so the single record it holds is padded out to a base alignment on its
    // own. The other two pad only their tail, so the region that follows starts aligned.
    const VkDeviceSize raygenSize = AlignUp<VkDeviceSize>(handleStride, baseAlignment);
    const VkDeviceSize missSize = AlignUp<VkDeviceSize>(VkDeviceSize(handleStride) * missCount, baseAlignment);
    const VkDeviceSize hitSize = AlignUp<VkDeviceSize>(VkDeviceSize(handleStride) * hitCount, baseAlignment);
    const VkDeviceSize tableSize = raygenSize + missSize + hitSize;

    // One base alignment of slack for the same reason the acceleration structure scratch
    // over-allocates: what has to be aligned is the device address the driver is handed,
    // and VMA only aligns the allocation behind it.
    m_ShaderBindingTable = VulkanBuffer::CreateMapped(ctx, tableSize + baseAlignment,
      SHADER_BINDING_TABLE_USAGE);

    const VkDeviceAddress bufferAddress = m_ShaderBindingTable.GetDeviceAddress(ctx);
    const VkDeviceAddress tableAddress = AlignUp<VkDeviceAddress>(bufferAddress, baseAlignment);

    auto* table = static_cast<uint8_t*>(m_ShaderBindingTable.GetMapped()) + (tableAddress - bufferAddress);
    // The padding between records is read by nothing, but a table full of allocation
    // garbage makes a capture unreadable and costs one memset per pipeline creation.
    std::memset(table, 0, tableSize);

    auto writeHandle = [&](uint32_t group, VkDeviceSize offset)
    {
      std::memcpy(table + offset, handles.data() + static_cast<size_t>(group) * handleSize, handleSize);
    };

    writeHandle(0, 0);
    for (uint32_t i = 0; i < missCount; i++)
      writeHandle(1 + i, raygenSize + VkDeviceSize(i) * handleStride);
    for (uint32_t i = 0; i < hitCount; i++)
      writeHandle(1 + missCount + i, raygenSize + missSize + VkDeviceSize(i) * handleStride);

    // Raygen is the only region whose size is its stride; the other two are strided runs of
    // records. An absent region is left fully zeroed rather than pointed at empty space -
    // that is how vkCmdTraceRaysKHR is told there are none of that kind.
    m_RaygenRegion = {};
    m_RaygenRegion.deviceAddress = tableAddress;
    m_RaygenRegion.stride = raygenSize;
    m_RaygenRegion.size = raygenSize;

    m_MissRegion = {};
    if (missCount > 0)
    {
      m_MissRegion.deviceAddress = tableAddress + raygenSize;
      m_MissRegion.stride = handleStride;
      m_MissRegion.size = missSize;
    }

    m_HitRegion = {};
    if (hitCount > 0)
    {
      m_HitRegion.deviceAddress = tableAddress + raygenSize + missSize;
      m_HitRegion.stride = handleStride;
      m_HitRegion.size = hitSize;
    }

    m_CallableRegion = {};

    YA_LOG_VERBOSE("Render",
      "Shader binding table: %u group(s), handle %u at stride %u, regions raygen %llu / miss %llu / hit %llu bytes",
      groupCount, handleSize, handleStride,
      (unsigned long long)raygenSize, (unsigned long long)missSize, (unsigned long long)hitSize);
  }

  void VulkanRaytracingPipeline::Destroy()
  {
    if (m_Ctx == nullptr)
      return;

    if (m_Pipeline != VK_NULL_HANDLE)
      vkDestroyPipeline(m_Ctx->device, m_Pipeline, nullptr);
    if (m_PipelineLayout != VK_NULL_HANDLE)
      vkDestroyPipelineLayout(m_Ctx->device, m_PipelineLayout, nullptr);
    m_ShaderBindingTable.Destroy(*m_Ctx);

    m_Pipeline = VK_NULL_HANDLE;
    m_PipelineLayout = VK_NULL_HANDLE;
    m_RaygenRegion = {};
    m_MissRegion = {};
    m_HitRegion = {};
    m_CallableRegion = {};
  }

  void VulkanRaytracingPipeline::Bind(VkCommandBuffer cmd)
  {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_Pipeline);
  }

  void VulkanRaytracingPipeline::BindDescriptorSets(VkCommandBuffer cmd,
    std::initializer_list<VkDescriptorSet> descriptorSets, uint32_t set)
  {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
      m_PipelineLayout, set,
      static_cast<uint32_t>(descriptorSets.size()),
      descriptorSets.begin(), 0, nullptr);
  }

  void VulkanRaytracingPipeline::PushConstants(VkCommandBuffer cmd, const void* data)
  {
    vkCmdPushConstants(cmd, m_PipelineLayout, m_PushConstantStages,
      0, m_PushConstantSize, data);
  }

  void VulkanRaytracingPipeline::TraceRays(VkCommandBuffer cmd,
    uint32_t width, uint32_t height, uint32_t depth)
  {
    m_Ctx->rayTracing.cmdTraceRays(cmd,
      &m_RaygenRegion, &m_MissRegion, &m_HitRegion, &m_CallableRegion,
      width, height, depth);
  }
}
