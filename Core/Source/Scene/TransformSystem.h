#pragma once

#include <entt/entt.hpp>
#include "Components.h"
#include "Scene/ISystem.h"

namespace YAEngine
{
  class TransformSystem : public ISystem
  {
  public:
    void Update(entt::registry& registry, double dt) override;
    SystemPhase GetPhase() const override { return SystemPhase::TransformUpdate; }

  private:
    static void UpdateWorldTransform(entt::registry& registry, entt::entity e, uint64_t tick);
    static glm::mat4 ComposeLocal(const LocalTransform& t);

    // One Update call = one tick, stamped into WorldTransform on every actual
    // recompute. Monotonic per system instance, which is all the shadow cache
    // needs: a change between two frames always changes the stamp.
    uint64_t m_Tick = 0;
  };
}
