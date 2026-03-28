#pragma once

#include <entt/entt.hpp>
#include "Components.h"
#include "Scene/ISystem.h"

namespace YAEngine
{
  class BoundsUpdateSystem : public ISystem
  {
  public:
    void Update(entt::registry& registry, double dt) override;
    SystemPhase GetPhase() const override { return SystemPhase::TransformUpdate; }
    int GetPriority() const override { return 100; }
  };
}
