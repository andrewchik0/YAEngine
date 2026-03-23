#pragma once

#include <entt/entt.hpp>
#include "Components.h"

namespace YAEngine
{
  class TransformSystem
  {
  public:
    static void Update(entt::registry& registry);

  private:
    static void UpdateWorldTransform(entt::registry& registry, entt::entity e);
    static glm::mat4 ComposeLocal(const TransformComponent& t);
  };
}
