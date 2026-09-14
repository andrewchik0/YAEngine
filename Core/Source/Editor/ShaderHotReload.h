#pragma once

#include "Pch.h"
#include "Editor/ShaderCompiler.h"
#include "Editor/ShaderDependencyGraph.h"

namespace YAEngine
{
  class PipelineCache;
  class ThreadPool;

  class ShaderHotReload
  {
  public:
    void Init(PipelineCache* psoCache, VkDevice device, ThreadPool* threadPool);
    // True only when pipelines were actually recreated - callers holding rendered output
    // from the replaced pipelines (e.g. the shadow atlas cache) must drop it.
    bool Update(double currentTime);
    // Returns the number of shaders queued; 0 while an earlier batch is still compiling.
    uint32_t RecompileAll();
    bool IsCompiling() const { return m_PendingBatch.has_value(); }
    // Shaders of the last finished batch that did not compile.
    uint32_t GetLastBatchFailureCount() const { return m_LastBatchFailureCount; }
    void Destroy();

  private:
    bool ProcessCompilationResults();

    PipelineCache* m_PSOCache = nullptr;
    VkDevice m_Device = VK_NULL_HANDLE;
    ThreadPool* m_ThreadPool = nullptr;

    ShaderCompiler m_Compiler;
    ShaderDependencyGraph m_DependencyGraph;

    struct PendingBatch
    {
      struct Entry
      {
        std::string outputName;
        std::future<CompileResult> future;
      };
      std::vector<Entry> entries;
    };
    std::optional<PendingBatch> m_PendingBatch;
    uint32_t m_LastBatchFailureCount = 0;

    double m_LastPollTime = 0.0;
    static constexpr double POLL_INTERVAL = 0.5;
  };
}
