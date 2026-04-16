#include "CollisionQueryService.h"

#include "Scene.h"
#include "Components.h"
#include "Utils/Log.h"

namespace YAEngine
{
  static bool OverlapAABBvsAABB(const glm::vec3& aMin, const glm::vec3& aMax,
                                const glm::vec3& bMin, const glm::vec3& bMax)
  {
    return aMin.x <= bMax.x && aMax.x >= bMin.x
        && aMin.y <= bMax.y && aMax.y >= bMin.y
        && aMin.z <= bMax.z && aMax.z >= bMin.z;
  }

  std::vector<entt::entity> CollisionQueryService::OverlapAABB(const glm::vec3& worldMin,
                                                               const glm::vec3& worldMax,
                                                               uint32_t mask) const
  {
    // diagnose invalid query AABB so callers can see their own bug in logs
    if (worldMin.x > worldMax.x || worldMin.y > worldMax.y || worldMin.z > worldMax.z)
    {
      YA_LOG_WARN("Physics", "CollisionQueryService::OverlapAABB called with inverted bounds");
      return {};
    }

    static bool s_FirstCall = true;
    if (s_FirstCall)
    {
      YA_LOG_INFO("Physics", "CollisionQueryService active");
      s_FirstCall = false;
    }

    std::vector<entt::entity> result;
    auto& registry = m_Scene.GetRegistry();

    auto singleView = registry.view<ColliderComponent, WorldTransform>();
    for (auto entity : singleView)
    {
      auto& collider = singleView.get<ColliderComponent>(entity);
      if ((collider.layer & mask) == 0) continue;

      auto& wt = singleView.get<WorldTransform>(entity);
      glm::vec3 center = glm::vec3(wt.world * glm::vec4(collider.localOffset, 1.0f));

      // extract scale from the world matrix basis vectors (ignore rotation for MVP)
      glm::vec3 scale(
        glm::length(glm::vec3(wt.world[0])),
        glm::length(glm::vec3(wt.world[1])),
        glm::length(glm::vec3(wt.world[2])));

      glm::vec3 scaledHalf = collider.halfExtents * scale;
      glm::vec3 aMin = center - scaledHalf;
      glm::vec3 aMax = center + scaledHalf;

      if (OverlapAABBvsAABB(aMin, aMax, worldMin, worldMax))
        result.push_back(entity);
    }

    auto instancedView = registry.view<InstancedColliderComponent>();
    for (auto entity : instancedView)
    {
      auto& instanced = instancedView.get<InstancedColliderComponent>(entity);
      if ((instanced.layer & mask) == 0) continue;

      for (auto& entry : instanced.instances)
      {
        glm::vec3 aMin = entry.center - entry.halfExtents;
        glm::vec3 aMax = entry.center + entry.halfExtents;
        if (OverlapAABBvsAABB(aMin, aMax, worldMin, worldMax))
        {
          result.push_back(entity);
          break;
        }
      }
    }

    return result;
  }
}
