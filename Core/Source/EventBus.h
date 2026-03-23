#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include <typeindex>
#include <cstdint>
#include <algorithm>

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
      auto& handlers = m_Handlers[typeid(Event)];
      HandlerWrapper wrapper {id, priority, [handler](const void* e)
      {
          return handler(*static_cast<const Event*>(e));
      }};
      InsertSorted(handlers, std::move(wrapper));
      return id;
    }

    // Overload for void callbacks (backwards compatible, never consumes)
    template<typename Event>
    SubscriptionId Subscribe(std::function<void(const Event&)> handler, int priority = 0)
    {
      auto id = ++m_NextId;
      auto& handlers = m_Handlers[typeid(Event)];
      HandlerWrapper wrapper {id, priority, [handler](const void* e)
      {
          handler(*static_cast<const Event*>(e));
          return false;
      }};
      InsertSorted(handlers, std::move(wrapper));
      return id;
    }

    template<typename Event>
    void Unsubscribe(SubscriptionId id)
    {
      auto it = m_Handlers.find(typeid(Event));
      if (it == m_Handlers.end()) return;

      auto& vec = it->second;
      vec.erase(
          std::remove_if(vec.begin(), vec.end(),
              [id](const HandlerWrapper& h){ return h.id == id; }),
          vec.end());
    }

    template<typename Event>
    void Emit(const Event& event)
    {
      auto it = m_Handlers.find(typeid(Event));
      if (it == m_Handlers.end()) return;

      for (auto& h : it->second)
      {
        if (h.callback(&event))
          break;
      }
    }

  private:
    struct HandlerWrapper
    {
      SubscriptionId id;
      int priority;
      std::function<bool(const void*)> callback;
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

    std::unordered_map<std::type_index, std::vector<HandlerWrapper>> m_Handlers;
    SubscriptionId m_NextId{0};
  };
}
