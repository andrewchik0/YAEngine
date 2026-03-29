#include "Editor/ShaderHotReload.h"
#include "Render/PipelineCache.h"
#include "Utils/ThreadPool.h"
#include "Utils/Log.h"

namespace YAEngine
{
  void ShaderHotReload::Init(PipelineCache* psoCache, VkDevice device, ThreadPool* threadPool)
  {
    m_PSOCache = psoCache;
    m_Device = device;
    m_ThreadPool = threadPool;

    m_Compiler.Init(SHADER_SOURCE_DIR, SHADER_BIN_DIR, COMPILE_SHADERS_EXE, GLSLC_PATH);

    m_DependencyGraph.Build(SHADER_SOURCE_DIR, SHADER_SHARED_DIR);
    m_DependencyGraph.LoadPermutationsFromCSV(SHADER_PERMUTATIONS_FILE);

    YA_LOG_INFO("ShaderHotReload", "Initialized (polling every %.1fs)", POLL_INTERVAL);
  }

  void ShaderHotReload::Destroy()
  {
    // If there's a pending batch, wait for all futures
    if (m_PendingBatch)
    {
      for (auto& entry : m_PendingBatch->entries)
        entry.future.wait();
      m_PendingBatch.reset();
    }
  }

  void ShaderHotReload::Update(double currentTime)
  {
    // Check if there's a pending compilation batch to process
    if (m_PendingBatch)
    {
      ProcessCompilationResults();
      return;
    }

    // Throttle polling
    if (currentTime - m_LastPollTime < POLL_INTERVAL)
      return;
    m_LastPollTime = currentTime;

    auto tasks = m_DependencyGraph.PollChanges();
    if (tasks.empty())
      return;

    YA_LOG_INFO("ShaderHotReload", "Detected changes, compiling %u shader(s)...",
      static_cast<uint32_t>(tasks.size()));

    // Submit all compilations to the thread pool
    PendingBatch batch;
    for (auto& task : tasks)
    {
      std::string outputName = task.outputName;
      auto future = m_ThreadPool->Submit(
        [compiler = &m_Compiler, t = std::move(task)]()
        {
          return compiler->Compile(t.sourceFile, t.outputName, t.defines);
        });

      batch.entries.push_back({ std::move(outputName), std::move(future) });
    }

    m_PendingBatch = std::move(batch);
  }

  void ShaderHotReload::RecompileAll()
  {
    if (m_PendingBatch)
      return;

    auto tasks = m_DependencyGraph.GetAllCompileTasks();
    if (tasks.empty())
      return;

    YA_LOG_INFO("ShaderHotReload", "Force recompiling all %u shader(s)...",
      static_cast<uint32_t>(tasks.size()));

    PendingBatch batch;
    for (auto& task : tasks)
    {
      std::string outputName = task.outputName;
      auto future = m_ThreadPool->Submit(
        [compiler = &m_Compiler, t = std::move(task)]()
        {
          return compiler->Compile(t.sourceFile, t.outputName, t.defines);
        });

      batch.entries.push_back({ std::move(outputName), std::move(future) });
    }

    m_PendingBatch = std::move(batch);
  }

  void ShaderHotReload::ProcessCompilationResults()
  {
    // Check if all futures are ready
    for (auto& entry : m_PendingBatch->entries)
    {
      if (entry.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    }

    // All done — collect results
    bool allSuccess = true;
    std::vector<std::string> successOutputs;

    for (auto& entry : m_PendingBatch->entries)
    {
      auto result = entry.future.get();
      if (result.success)
      {
        successOutputs.push_back(entry.outputName);
      }
      else
      {
        allSuccess = false;
        YA_LOG_ERROR("ShaderHotReload", "Failed to compile '%s': %s",
          entry.outputName.c_str(), result.errorMessage.c_str());
      }
    }

    m_PendingBatch.reset();

    if (successOutputs.empty())
      return;

    if (!allSuccess)
    {
      YA_LOG_WARN("ShaderHotReload", "Some shaders failed to compile, skipping pipeline recreation");
      return;
    }

    // Called from Update() on the main thread — safe to wait and recreate directly
    vkDeviceWaitIdle(m_Device);
    uint32_t total = 0;
    for (auto& spvName : successOutputs)
    {
      // Strip .spv to get the shader file name used as key in PipelineCache
      // e.g. "shader.frag.spv" → "shader.frag"
      std::string shaderKey = spvName;
      if (shaderKey.ends_with(".spv"))
        shaderKey.resize(shaderKey.size() - 4);

      m_PSOCache->RecreatePipelinesForShader(m_Device, shaderKey);
      ++total;
    }

    YA_LOG_INFO("ShaderHotReload", "Reloaded %u shader(s)", total);
  }
}
