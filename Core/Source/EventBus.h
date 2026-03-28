#pragma once

#include "Pch.h"

namespace YAEngine
{
  using SubscriptionId = uint64_t;

  class EventBus
  {
  public:

    // Subscribe with priority (lower = earlier). Callback returns bool: true = consumed.
    template<typename Event>
    SubscriptionId Subscribe(std::function<bool(const Event&)> handler, int priority = 0)
    {
      auto id = ++m_NextId;
      auto& list = m_Handlers[typeid(Event)];
      HandlerWrapper wrapper {id, priority, false, [handler](const void* e)
      {
          return handler(*static_cast<const Event*>(e));
      }};

      if (list.iterating)
        list.pendingAdds.push_back(std::move(wrapper));
      else
        InsertSorted(list.handlers, std::move(wrapper));

      return id;
    }

    // Overload for void callbacks (backwards compatible, never consumes)
    template<typename Event>
    SubscriptionId Subscribe(std::function<void(const Event&)> handler, int priority = 0)
    {
      auto id = ++m_NextId;
      auto& list = m_Handlers[typeid(Event)];
      HandlerWrapper wrapper {id, priority, false, [handler](const void* e)
      {
          handler(*static_cast<const Event*>(e));
          return false;
      }};

      if (list.iterating)
        list.pendingAdds.push_back(std::move(wrapper));
      else
        InsertSorted(list.handlers, std::move(wrapper));

      return id;
    }

    template<typename Event>
    void Unsubscribe(SubscriptionId id)
    {
      auto it = m_Handlers.find(typeid(Event));
      if (it == m_Handlers.end()) return;

      auto& list = it->second;
      if (list.iterating)
      {
        for (auto& h : list.handlers)
          if (h.id == id) { h.removed = true; return; }
      }
      else
      {
        auto& vec = list.handlers;
        vec.erase(
            std::remove_if(vec.begin(), vec.end(),
                [id](const HandlerWrapper& h){ return h.id == id; }),
            vec.end());
      }
    }

    template<typename Event>
    void Emit(const Event& event)
    {
      auto it = m_Handlers.find(typeid(Event));
      if (it == m_Handlers.end()) return;

      auto& list = it->second;
      list.iterating = true;

      for (size_t i = 0; i < list.handlers.size(); i++)
      {
        auto& h = list.handlers[i];
        if (h.removed) continue;
        if (h.callback(&event))
          break;
      }

      list.iterating = false;
      ApplyPending(list);
    }

    template<typename Event>
    void EmitDeferred(const Event& event)
    {
      m_DeferredQueue.push_back([this, event]() { Emit(event); });
    }

    void FlushDeferred()
    {
      auto queue = std::move(m_DeferredQueue);
      for (auto& fn : queue)
        fn();
    }

  private:
    struct HandlerWrapper
    {
      SubscriptionId id;
      int priority;
      bool removed;
      std::function<bool(const void*)> callback;
    };

    struct HandlerList
    {
      std::vector<HandlerWrapper> handlers;
      std::vector<HandlerWrapper> pendingAdds;
      bool iterating = false;
    };

    static void InsertSorted(std::vector<HandlerWrapper>& handlers, HandlerWrapper&& wrapper)
    {
      auto pos = std::upper_bound(handlers.begin(), handlers.end(), wrapper,
        [](const HandlerWrapper& a, const HandlerWrapper& b)
        {
          return a.priority < b.priority;
        });
      handlers.insert(pos, std::move(wrapper));
    }

    static void ApplyPending(HandlerList& list)
    {
      // Remove handlers marked for deletion
      auto& vec = list.handlers;
      vec.erase(
        std::remove_if(vec.begin(), vec.end(),
          [](const HandlerWrapper& h){ return h.removed; }),
        vec.end());

      // Insert pending adds
      for (auto& wrapper : list.pendingAdds)
        InsertSorted(list.handlers, std::move(wrapper));
      list.pendingAdds.clear();
    }

    std::unordered_map<std::type_index, HandlerList> m_Handlers;
    std::vector<std::function<void()>> m_DeferredQueue;
    SubscriptionId m_NextId{0};
  };
}
