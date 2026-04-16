#pragma once

#include <entt/entt.hpp>
#include "Scene/ISystem.h"

namespace YAEngine
{
  class AssetManager;

  class ModelColliderSystem : public ISystem
  {
  public:
    explicit ModelColliderSystem(AssetManager& assets) : m_Assets(assets) {}

    void Update(entt::registry& registry, double dt) override;
    SystemPhase GetPhase() const override { return SystemPhase::Physics; }
    int GetPriority() const override { return 2; }

  private:
    AssetManager& m_Assets;
  };
}
