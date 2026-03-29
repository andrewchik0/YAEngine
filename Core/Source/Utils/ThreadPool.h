#pragma once

#include "Pch.h"
#include "Utils/Log.h"

namespace YAEngine
{
  class ThreadPool
  {
  public:
    explicit ThreadPool(uint32_t threadCount = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    template<typename F>
    auto Submit(F&& task) -> std::future<std::invoke_result_t<F>>
    {
      using R = std::invoke_result_t<F>;

      auto sharedTask = std::make_shared<std::packaged_task<R()>>(std::forward<F>(task));
      auto future = sharedTask->get_future();

      {
        std::lock_guard lock(m_Mutex);
        m_Tasks.emplace_back([sharedTask]() { (*sharedTask)(); });
      }
      m_Condition.notify_one();

      return future;
    }

    uint32_t GetThreadCount() const { return static_cast<uint32_t>(m_Workers.size()); }

    uint32_t GetQueueSize()
    {
      std::lock_guard lock(m_Mutex);
      return static_cast<uint32_t>(m_Tasks.size());
    }

  private:
    void WorkerLoop();

    std::atomic<bool> m_Stopping { false };
    std::mutex m_Mutex;
    std::condition_variable m_Condition;
    std::deque<std::function<void()>> m_Tasks;
    std::vector<std::thread> m_Workers;
  };
}
