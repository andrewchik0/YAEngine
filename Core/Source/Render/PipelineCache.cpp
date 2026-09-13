#include "PipelineCache.h"

#include "DebugMarker.h"
#include "RenderContext.h"
#include "Utils/Log.h"

namespace YAEngine
{

  bool GraphicsPipelineKey::operator==(const GraphicsPipelineKey& other) const
  {
    if (vertexShaderFile != other.vertexShaderFile) return false;
    if (fragmentShaderFile != other.fragmentShaderFile) return false;
    if (renderPass != other.renderPass) return false;
    if (depthTesting != other.depthTesting) return false;
    if (depthWrite != other.depthWrite) return false;
    if (blending != other.blending) return false;
    if (additiveBlend != other.additiveBlend) return false;
    if (premultipliedAlpha != other.premultipliedAlpha) return false;
    if (doubleSided != other.doubleSided) return false;
    if (compareOp != other.compareOp) return false;
    if (topology != other.topology) return false;
    if (depthBiasEnable != other.depthBiasEnable) return false;
    if (depthClampEnable != other.depthClampEnable) return false;
    if (dynamicCullMode != other.dynamicCullMode) return false;
    if (colorAttachmentCount != other.colorAttachmentCount) return false;
    if (pushConstantSize != other.pushConstantSize) return false;
    if (vertexInputFormat != other.vertexInputFormat) return false;
    if (sets.size() != other.sets.size()) return false;
    for (size_t i = 0; i < sets.size(); i++)
    {
      if (sets[i] != other.sets[i]) return false;
    }
    return true;
  }

  size_t GraphicsPipelineKeyHash::operator()(const GraphicsPipelineKey& key) const
  {
    size_t hash = 0;
    auto combine = [&](size_t v)
    {
      hash ^= v + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    };

    combine(std::hash<std::string>{}(key.vertexShaderFile));
    combine(std::hash<std::string>{}(key.fragmentShaderFile));
    combine(std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(key.renderPass)));
    combine(std::hash<bool>{}(key.depthTesting));
    combine(std::hash<bool>{}(key.depthWrite));
    combine(std::hash<bool>{}(key.blending));
    combine(std::hash<bool>{}(key.additiveBlend));
    combine(std::hash<bool>{}(key.premultipliedAlpha));
    combine(std::hash<bool>{}(key.doubleSided));
    combine(std::hash<uint32_t>{}(static_cast<uint32_t>(key.compareOp)));
    combine(std::hash<uint32_t>{}(static_cast<uint32_t>(key.topology)));
    combine(std::hash<bool>{}(key.depthBiasEnable));
    combine(std::hash<bool>{}(key.depthClampEnable));
    combine(std::hash<bool>{}(key.dynamicCullMode));
    combine(std::hash<uint32_t>{}(key.colorAttachmentCount));
    combine(std::hash<uint32_t>{}(key.pushConstantSize));
    combine(std::hash<std::string>{}(key.vertexInputFormat));
    for (auto layout : key.sets)
      combine(std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(layout)));

    return hash;
  }

  bool ComputePipelineKey::operator==(const ComputePipelineKey& other) const
  {
    if (shaderFile != other.shaderFile) return false;
    if (pushConstantSize != other.pushConstantSize) return false;
    if (sets.size() != other.sets.size()) return false;
    for (size_t i = 0; i < sets.size(); i++)
    {
      if (sets[i] != other.sets[i]) return false;
    }
    return true;
  }

  size_t ComputePipelineKeyHash::operator()(const ComputePipelineKey& key) const
  {
    size_t hash = 0;
    auto combine = [&](size_t v)
    {
      hash ^= v + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    };

    combine(std::hash<std::string>{}(key.shaderFile));
    combine(std::hash<uint32_t>{}(key.pushConstantSize));
    for (auto layout : key.sets)
      combine(std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(layout)));

    return hash;
  }

  bool RayTracingPipelineKey::operator==(const RayTracingPipelineKey& other) const
  {
    if (pushConstantSize != other.pushConstantSize) return false;
    if (shaderFiles != other.shaderFiles) return false;
    if (sets.size() != other.sets.size()) return false;
    for (size_t i = 0; i < sets.size(); i++)
    {
      if (sets[i] != other.sets[i]) return false;
    }
    return true;
  }

  size_t RayTracingPipelineKeyHash::operator()(const RayTracingPipelineKey& key) const
  {
    size_t hash = 0;
    auto combine = [&](size_t v)
    {
      hash ^= v + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    };

    for (const auto& file : key.shaderFiles)
      combine(std::hash<std::string>{}(file));
    combine(std::hash<uint32_t>{}(key.pushConstantSize));
    for (auto layout : key.sets)
      combine(std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(layout)));

    return hash;
  }

  RayTracingPipelineKey PipelineCache::MakeRayTracingKey(const RaytracingPipelineCreateInfo& info)
  {
    RayTracingPipelineKey key = {
      .pushConstantSize = info.pushConstantSize,
      .sets = info.sets,
    };

    key.shaderFiles.push_back(info.raygenShaderFile);
    for (const auto& missShaderFile : info.missShaderFiles)
      key.shaderFiles.push_back(missShaderFile);
    for (const auto& hitGroup : info.hitGroups)
    {
      key.shaderFiles.push_back(hitGroup.closestHitShaderFile);
      // Pushed even when empty, so a group that gained or lost its any-hit shader is a
      // different key rather than the same one with a shorter list.
      key.shaderFiles.push_back(hitGroup.anyHitShaderFile);
    }

    return key;
  }

  VulkanPipeline& PipelineCache::GetOrCreate(
    VkDevice device,
    VkRenderPass renderPass,
    const PipelineCreateInfo& info,
    VkPipelineCache vkCache)
  {
    GraphicsPipelineKey key = {
      .vertexShaderFile = info.vertexShaderFile,
      .fragmentShaderFile = info.fragmentShaderFile,
      .renderPass = renderPass,
      .depthTesting = info.depthTesting,
      .depthWrite = info.depthWrite,
      .blending = info.blending,
      .additiveBlend = info.additiveBlend,
      .premultipliedAlpha = info.premultipliedAlpha,
      .doubleSided = info.doubleSided,
      .compareOp = info.compareOp,
      .topology = info.topology,
      .depthBiasEnable = info.depthBiasEnable,
      .depthClampEnable = info.depthClampEnable,
      .dynamicCullMode = info.dynamicCullMode,
      .colorAttachmentCount = info.colorAttachmentCount,
      .pushConstantSize = info.pushConstantSize,
      .vertexInputFormat = info.vertexInputFormat,
      .sets = info.sets,
    };

    auto [it, inserted] = m_GraphicsCache.try_emplace(std::move(key));
    if (!inserted)
      return it->second;

    auto& pipeline = it->second;
    pipeline.Init(device, renderPass, info, vkCache);
    YA_DEBUG_NAMEF(device, VK_OBJECT_TYPE_PIPELINE,
      pipeline.Get(), "%s + %s", info.vertexShaderFile.c_str(), info.fragmentShaderFile.c_str());
    YA_LOG_VERBOSE("Render", "PSO Cache: created graphics pipeline (%s + %s)",
      info.vertexShaderFile.c_str(), info.fragmentShaderFile.c_str());
    return pipeline;
  }

  VulkanComputePipeline& PipelineCache::GetOrCreateCompute(
    VkDevice device,
    const std::string& shaderFile,
    const std::vector<VkDescriptorSetLayout>& sets,
    uint32_t pushConstantSize,
    VkPipelineCache vkCache)
  {
    ComputePipelineKey key = {
      .shaderFile = shaderFile,
      .pushConstantSize = pushConstantSize,
      .sets = sets,
    };

    auto [it, inserted] = m_ComputeCache.try_emplace(std::move(key));
    if (!inserted)
      return it->second;

    auto& pipeline = it->second;
    pipeline.Init(device, shaderFile, sets, pushConstantSize, vkCache);
    YA_DEBUG_NAME(device, VK_OBJECT_TYPE_PIPELINE,
      pipeline.Get(), shaderFile.c_str());
    YA_LOG_VERBOSE("Render", "PSO Cache: created compute pipeline (%s)", shaderFile.c_str());
    return pipeline;
  }

  VulkanRaytracingPipeline& PipelineCache::GetOrCreateRayTracing(
    const RenderContext& ctx,
    const RayTracingPipelineKey& key,
    const RaytracingPipelineCreateInfo& info,
    VkPipelineCache vkCache)
  {
    auto [it, inserted] = m_RayTracingCache.try_emplace(key);
    if (!inserted)
      return it->second;

    auto& pipeline = it->second;
    pipeline.Init(ctx, info, vkCache);
    YA_DEBUG_NAME(ctx.device, VK_OBJECT_TYPE_PIPELINE,
      pipeline.Get(), info.raygenShaderFile.c_str());
    YA_LOG_VERBOSE("Render", "PSO Cache: created ray tracing pipeline (%s, %u miss, %u hit group(s))",
      info.raygenShaderFile.c_str(),
      static_cast<uint32_t>(info.missShaderFiles.size()),
      static_cast<uint32_t>(info.hitGroups.size()));
    return pipeline;
  }

  PipelineHandle PipelineCache::Register(
    VkDevice device,
    VkRenderPass renderPass,
    const PipelineCreateInfo& info,
    VkPipelineCache vkCache)
  {
    auto& pipeline = GetOrCreate(device, renderPass, info, vkCache);
    uint32_t index = static_cast<uint32_t>(m_GraphicsPipelines.size());
    m_GraphicsPipelines.push_back(&pipeline);
    PipelineHandle handle { index };

#ifdef YA_EDITOR
    m_GraphicsEntries.push_back({ &pipeline, info, renderPass, vkCache });
    if (!info.vertexShaderFile.empty())
      m_ShaderToGraphics[info.vertexShaderFile].push_back(handle);
    if (!info.fragmentShaderFile.empty())
      m_ShaderToGraphics[info.fragmentShaderFile].push_back(handle);
    if (!info.geometryShaderFile.empty())
      m_ShaderToGraphics[info.geometryShaderFile].push_back(handle);
#endif

    return handle;
  }

  PipelineHandle PipelineCache::RegisterCompute(
    VkDevice device,
    const std::string& shaderFile,
    const std::vector<VkDescriptorSetLayout>& sets,
    uint32_t pushConstantSize,
    VkPipelineCache vkCache)
  {
    auto& pipeline = GetOrCreateCompute(device, shaderFile, sets, pushConstantSize, vkCache);
    uint32_t index = static_cast<uint32_t>(m_ComputePipelines.size());
    m_ComputePipelines.push_back(&pipeline);
    PipelineHandle handle { index };

#ifdef YA_EDITOR
    m_ComputeEntries.push_back({ &pipeline, shaderFile, sets, pushConstantSize, vkCache });
    m_ShaderToCompute[shaderFile].push_back(handle);
#endif

    return handle;
  }

  PipelineHandle PipelineCache::RegisterRayTracing(
    const RenderContext& ctx,
    const RaytracingPipelineCreateInfo& info,
    VkPipelineCache vkCache)
  {
    RayTracingPipelineKey key = MakeRayTracingKey(info);
    auto& pipeline = GetOrCreateRayTracing(ctx, key, info, vkCache);
    uint32_t index = static_cast<uint32_t>(m_RayTracingPipelines.size());
    m_RayTracingPipelines.push_back(&pipeline);
    PipelineHandle handle { index };

#ifdef YA_EDITOR
    m_RayTracingEntries.push_back({ &pipeline, &ctx, info, vkCache });

    // Deduplicated on the way in: the same closest hit shader can serve several groups of
    // one pipeline, and recreating that pipeline twice for one reload would destroy a
    // pipeline the second pass then rebuilds from a handle nothing else holds.
    for (const auto& shaderFile : key.shaderFiles)
    {
      if (shaderFile.empty())
        continue;

      auto& handles = m_ShaderToRayTracing[shaderFile];
      if (std::find_if(handles.begin(), handles.end(),
        [index](PipelineHandle existing) { return existing.index == index; }) == handles.end())
      {
        handles.push_back(handle);
      }
    }
#endif

    return handle;
  }

  VulkanPipeline& PipelineCache::Get(PipelineHandle handle)
  {
    return *m_GraphicsPipelines[handle.index];
  }

  VulkanComputePipeline& PipelineCache::GetCompute(PipelineHandle handle)
  {
    return *m_ComputePipelines[handle.index];
  }

  VulkanRaytracingPipeline& PipelineCache::GetRayTracing(PipelineHandle handle)
  {
    return *m_RayTracingPipelines[handle.index];
  }

  void PipelineCache::Destroy()
  {
    for (auto& [key, pipeline] : m_GraphicsCache)
      pipeline.Destroy();
    m_GraphicsCache.clear();

    for (auto& [key, pipeline] : m_ComputeCache)
      pipeline.Destroy();
    m_ComputeCache.clear();

    // Releases the shader binding table with the pipeline, which is why this has to run
    // before the backend tears the allocator down.
    for (auto& [key, pipeline] : m_RayTracingCache)
      pipeline.Destroy();
    m_RayTracingCache.clear();

    m_GraphicsPipelines.clear();
    m_ComputePipelines.clear();
    m_RayTracingPipelines.clear();

#ifdef YA_EDITOR
    m_GraphicsEntries.clear();
    m_ComputeEntries.clear();
    m_RayTracingEntries.clear();
    m_ShaderToGraphics.clear();
    m_ShaderToCompute.clear();
    m_ShaderToRayTracing.clear();
#endif
  }

#ifdef YA_EDITOR
  void PipelineCache::RecreatePipelinesForShader(VkDevice device, const std::string& shaderFile)
  {
    uint32_t count = 0;

    auto gIt = m_ShaderToGraphics.find(shaderFile);
    if (gIt != m_ShaderToGraphics.end())
    {
      for (auto handle : gIt->second)
      {
        auto& entry = m_GraphicsEntries[handle.index];
        entry.pipeline->Destroy();
        entry.pipeline->Init(device, entry.renderPass, entry.info, entry.vkCache);
        ++count;
      }
    }

    auto cIt = m_ShaderToCompute.find(shaderFile);
    if (cIt != m_ShaderToCompute.end())
    {
      for (auto handle : cIt->second)
      {
        auto& entry = m_ComputeEntries[handle.index];
        entry.pipeline->Destroy();
        entry.pipeline->Init(device, entry.shaderFile, entry.sets, entry.pushConstantSize, entry.vkCache);
        ++count;
      }
    }

    // Init rebuilds the shader binding table along with the pipeline, which is the whole
    // reason the entry keeps a context: group handles belong to one pipeline object and
    // mean nothing for its replacement.
    auto rtIt = m_ShaderToRayTracing.find(shaderFile);
    if (rtIt != m_ShaderToRayTracing.end())
    {
      for (auto handle : rtIt->second)
      {
        auto& entry = m_RayTracingEntries[handle.index];
        entry.pipeline->Destroy();
        entry.pipeline->Init(*entry.ctx, entry.info, entry.vkCache);
        ++count;
      }
    }

    if (count > 0)
      YA_LOG_INFO("Render", "Recreated %u pipeline(s) for shader '%s'", count, shaderFile.c_str());
  }

#endif

}
