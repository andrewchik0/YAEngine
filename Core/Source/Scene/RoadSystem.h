#pragma once

#include <entt/entt.hpp>
#include "Scene/ISystem.h"
#include "Assets/MeshManager.h"

namespace YAEngine
{
  class AssetManager;

  class RoadSystem : public ISystem
  {
  public:
    explicit RoadSystem(AssetManager& assets) : m_Assets(assets) {}

    void Update(entt::registry& registry, double dt) override;
    void OnSceneClear() override;
    SystemPhase GetPhase() const override { return SystemPhase::Physics; }

  private:
    static constexpr uint32_t DESTROY_DELAY_FRAMES = 3;

    struct PendingDestroy
    {
      MeshHandle handle;
      uint32_t framesLeft;
    };

    AssetManager& m_Assets;
    std::vector<PendingDestroy> m_PendingDestroys;
  };
}
