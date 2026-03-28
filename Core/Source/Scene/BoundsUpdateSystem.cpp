#include "BoundsUpdateSystem.h"

namespace YAEngine
{
  void BoundsUpdateSystem::Update(entt::registry& registry, double dt)
  {
    auto view = registry.view<BoundsDirty, LocalBounds, WorldTransform>();
    for (auto e : view)
    {
      auto& lb = registry.get<LocalBounds>(e);
      auto& wt = registry.get<WorldTransform>(e);

      glm::vec3 corners[8] = {
        { lb.min.x, lb.min.y, lb.min.z },
        { lb.max.x, lb.min.y, lb.min.z },
        { lb.min.x, lb.max.y, lb.min.z },
        { lb.max.x, lb.max.y, lb.min.z },
        { lb.min.x, lb.min.y, lb.max.z },
        { lb.max.x, lb.min.y, lb.max.z },
        { lb.min.x, lb.max.y, lb.max.z },
        { lb.max.x, lb.max.y, lb.max.z },
      };

      glm::vec3 worldMin( std::numeric_limits<float>::max());
      glm::vec3 worldMax(-std::numeric_limits<float>::max());

      for (auto& c : corners)
      {
        glm::vec3 wc = glm::vec3(wt.world * glm::vec4(c, 1.0f));
        worldMin = glm::min(worldMin, wc);
        worldMax = glm::max(worldMax, wc);
      }

      registry.emplace_or_replace<WorldBounds>(e, WorldBounds { .min = worldMin, .max = worldMax });
      registry.remove<BoundsDirty>(e);
    }
  }
}
