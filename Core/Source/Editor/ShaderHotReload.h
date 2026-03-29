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
    void Update(double currentTime);
    void RecompileAll();
    void Destroy();

  private:
    void ProcessCompilationResults();

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

    double m_LastPollTime = 0.0;
    static constexpr double POLL_INTERVAL = 0.5;
  };
}
