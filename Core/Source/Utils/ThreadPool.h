#pragma once

#include "Pch.h"
#include "Utils/Log.h"

namespace YAEngine
{
  class MoveOnlyFunction
  {
    struct IConcept
    {
      virtual ~IConcept() = default;
      virtual void Invoke() = 0;
    };

    template<typename F>
    struct Model : IConcept
    {
      F m_Func;
      explicit Model(F&& f) : m_Func(std::move(f)) {}
      void Invoke() override { m_Func(); }
    };

    std::unique_ptr<IConcept> m_Impl;

  public:
    MoveOnlyFunction() = default;

    // Accepts any callable. std::decay_t<F> strips references/const so we store a clean value type.
    // std::forward<F> preserves rvalue-ness so move-only callables get moved, not copied.
    template<typename F>
    MoveOnlyFunction(F&& f)
      : m_Impl(std::make_unique<Model<std::decay_t<F>>>(std::forward<F>(f))) {}

    void operator()() { m_Impl->Invoke(); }
    explicit operator bool() const { return m_Impl != nullptr; }
  };

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

      std::packaged_task<R()> packaged(std::forward<F>(task));
      auto future = packaged.get_future();

      {
        std::lock_guard lock(m_Mutex);
        m_Tasks.emplace_back([t = std::move(packaged)]() mutable { t(); });
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

    // Members are ordered so that m_Workers is destroyed first (threads join
    // before the mutex/condition/queue they use are destroyed)
    bool m_Stopping = false;
    std::mutex m_Mutex;
    std::condition_variable m_Condition;
    std::deque<MoveOnlyFunction> m_Tasks;
    std::vector<std::thread> m_Workers;
  };
}
