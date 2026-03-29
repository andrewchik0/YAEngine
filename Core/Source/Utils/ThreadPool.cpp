#include "Utils/ThreadPool.h"

namespace YAEngine
{
  ThreadPool::ThreadPool(uint32_t threadCount)
  {
    if (threadCount == 0)
    {
      // Leave one core for the main thread
      uint32_t hw = std::thread::hardware_concurrency();
      threadCount = hw > 1 ? hw - 1 : 1;
    }

    m_Workers.reserve(threadCount);
    for (uint32_t i = 0; i < threadCount; ++i)
      // &ThreadPool::WorkerLoop — pointer to member function
      // this — the object instance to call it on
      // std::thread will call this->WorkerLoop() on a new thread
      m_Workers.emplace_back(&ThreadPool::WorkerLoop, this);

    YA_LOG_INFO("ThreadPool", "Created %u worker threads", threadCount);
  }

  ThreadPool::~ThreadPool()
  {
    {
      std::lock_guard lock(m_Mutex);
      m_Stopping = true;
    }
    m_Condition.notify_all();

    for (auto& worker : m_Workers)
      worker.join();
  }

  void ThreadPool::WorkerLoop()
  {
    // Declared outside the loop so MoveOnlyFunction's internal buffer can be reused
    MoveOnlyFunction task;
    while (true)
    {
      {
        std::unique_lock lock(m_Mutex);
        // wait() atomically releases the lock and sleeps until notify is called.
        // On wake, it re-acquires the lock and checks the predicate.
        // If predicate returns false — goes back to sleep (spurious wakeup protection).
        m_Condition.wait(lock, [this] { return m_Stopping || !m_Tasks.empty(); });

        // On shutdown: finish remaining tasks before exiting, but once
        // the queue is drained, exit the loop
        if (m_Stopping && m_Tasks.empty())
          return;

        task = std::move(m_Tasks.front());
        m_Tasks.pop_front();
      }
      // Execute outside the lock so other workers can grab tasks concurrently
      task();
    }
  }
}
