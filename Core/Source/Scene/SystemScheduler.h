#pragma once

#include "Pch.h"
#include "Scene/ISystem.h"

namespace YAEngine
{
  class SystemScheduler
  {
  public:
    template<typename T, typename... Args>
    T& AddSystem(Args&&... args)
    {
      auto system = std::make_unique<T>(std::forward<Args>(args)...);
      T& ref = *system;
      m_Systems.push_back(std::move(system));
      b_Sorted = false;
      return ref;
    }

    void Run(entt::registry& registry, double dt);
    void NotifySceneClear();

  private:
    void SortIfNeeded();

    std::vector<std::unique_ptr<ISystem>> m_Systems;
    bool b_Sorted = false;
  };
}
