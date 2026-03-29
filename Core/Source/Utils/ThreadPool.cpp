#include "Utils/ThreadPool.h"

namespace YAEngine
{
  ThreadPool::ThreadPool(uint32_t threadCount)
  {
    if (threadCount == 0)
    {
      uint32_t hw = std::thread::hardware_concurrency();
      threadCount = hw > 1 ? hw - 1 : 1;
    }

    m_Workers.reserve(threadCount);
    for (uint32_t i = 0; i < threadCount; ++i)
      m_Workers.emplace_back(&ThreadPool::WorkerLoop, this);

    YA_LOG_INFO("ThreadPool", "Created %u worker threads", threadCount);
  }

  ThreadPool::~ThreadPool()
  {
    {
      std::lock_guard lock(m_Mutex);
      m_Stopping.store(true, std::memory_order_relaxed);
    }
    m_Condition.notify_all();

    for (auto& worker : m_Workers)
      worker.join();
  }

  void ThreadPool::WorkerLoop()
  {
    while (true)
    {
      std::function<void()> task;
      {
        std::unique_lock lock(m_Mutex);
        m_Condition.wait(lock, [this] { return m_Stopping.load(std::memory_order_relaxed) || !m_Tasks.empty(); });

        if (m_Stopping.load(std::memory_order_relaxed) && m_Tasks.empty())
          return;

        task = std::move(m_Tasks.front());
        m_Tasks.pop_front();
      }
      task();
    }
  }
}
