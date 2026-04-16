#pragma once

#include "Pch.h"
#include <entt/entt.hpp>

namespace YAEngine
{
  class Scene;

  class CollisionQueryService
  {
  public:
    explicit CollisionQueryService(Scene& scene) : m_Scene(scene) {}

    std::vector<entt::entity> OverlapAABB(const glm::vec3& worldMin,
                                          const glm::vec3& worldMax,
                                          uint32_t mask) const;

  private:
    Scene& m_Scene;
  };
}
