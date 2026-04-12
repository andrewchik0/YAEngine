#include "PipelineCache.h"

#include "DebugMarker.h"
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
    if (doubleSided != other.doubleSided) return false;
    if (compareOp != other.compareOp) return false;
    if (topology != other.topology) return false;
    if (depthBiasEnable != other.depthBiasEnable) return false;
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
    combine(std::hash<bool>{}(key.doubleSided));
    combine(std::hash<uint32_t>{}(static_cast<uint32_t>(key.compareOp)));
    combine(std::hash<uint32_t>{}(static_cast<uint32_t>(key.topology)));
    combine(std::hash<bool>{}(key.depthBiasEnable));
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
      .doubleSided = info.doubleSided,
      .compareOp = info.compareOp,
      .topology = info.topology,
      .depthBiasEnable = info.depthBiasEnable,
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

  VulkanPipeline& PipelineCache::Get(PipelineHandle handle)
  {
    return *m_GraphicsPipelines[handle.index];
  }

  VulkanComputePipeline& PipelineCache::GetCompute(PipelineHandle handle)
  {
    return *m_ComputePipelines[handle.index];
  }

  void PipelineCache::Destroy()
  {
    for (auto& [key, pipeline] : m_GraphicsCache)
      pipeline.Destroy();
    m_GraphicsCache.clear();

    for (auto& [key, pipeline] : m_ComputeCache)
      pipeline.Destroy();
    m_ComputeCache.clear();

    m_GraphicsPipelines.clear();
    m_ComputePipelines.clear();

#ifdef YA_EDITOR
    m_GraphicsEntries.clear();
    m_ComputeEntries.clear();
    m_ShaderToGraphics.clear();
    m_ShaderToCompute.clear();
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

    if (count > 0)
      YA_LOG_INFO("Render", "Recreated %u pipeline(s) for shader '%s'", count, shaderFile.c_str());
  }

#endif

}
