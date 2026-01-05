#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include <typeindex>
#include <cstdint>

namespace YAEngine
{
  using SubscriptionId = uint64_t;

  class EventBus
  {
  public:

    template<typename Event>
    SubscriptionId Subscribe(std::function<void(const Event&)> handler)
    {
      auto id = ++m_NextId;
      auto& handlers = m_Handlers[typeid(Event)];
      handlers.push_back({id, [handler](const void* e)
      {
          handler(*static_cast<const Event*>(e));
      }});
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
        h.callback(&event);
    }

  private:
    struct HandlerWrapper
    {
      SubscriptionId id;
      std::function<void(const void*)> callback;
    };

    std::unordered_map<std::type_index, std::vector<HandlerWrapper>> m_Handlers;
    SubscriptionId m_NextId{0};
  };
}