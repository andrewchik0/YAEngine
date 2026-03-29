#pragma once

#include "Pch.h"

namespace YAEngine
{
  class MainThreadDispatcher
  {
  public:
    MainThreadDispatcher() = default;

    MainThreadDispatcher(const MainThreadDispatcher&) = delete;
    MainThreadDispatcher& operator=(const MainThreadDispatcher&) = delete;
    MainThreadDispatcher(MainThreadDispatcher&&) = delete;
    MainThreadDispatcher& operator=(MainThreadDispatcher&&) = delete;

    template<typename F>
    void Dispatch(F&& task)
    {
      std::lock_guard lock(m_Mutex);
      m_PendingQueue.emplace_back(std::forward<F>(task));
    }

    void ProcessAll()
    {
      {
        std::lock_guard lock(m_Mutex);
        std::swap(m_PendingQueue, m_ExecutionQueue);
      }

      for (auto& task : m_ExecutionQueue)
        task();

      m_ExecutionQueue.clear();
    }

    uint32_t GetPendingCount()
    {
      std::lock_guard lock(m_Mutex);
      return static_cast<uint32_t>(m_PendingQueue.size());
    }

  private:
    std::vector<std::function<void()>> m_PendingQueue;
    std::vector<std::function<void()>> m_ExecutionQueue;
    std::mutex m_Mutex;
  };
}
