#include "Scene/SystemScheduler.h"

namespace YAEngine
{
  void SystemScheduler::Run(entt::registry& registry, double dt)
  {
    SortIfNeeded();
    for (auto& system : m_Systems)
      system->Update(registry, dt);
  }

  void SystemScheduler::NotifySceneClear()
  {
    for (auto& system : m_Systems)
      system->OnSceneClear();
  }

  void SystemScheduler::SortIfNeeded()
  {
    if (b_Sorted) return;

    std::sort(m_Systems.begin(), m_Systems.end(),
      [](const std::unique_ptr<ISystem>& a, const std::unique_ptr<ISystem>& b)
      {
        if (a->GetPhase() != b->GetPhase())
          return a->GetPhase() < b->GetPhase();
        return a->GetPriority() < b->GetPriority();
      });

    b_Sorted = true;
  }
}
