#pragma once

#include <entt/entt.hpp>
#include "Scene/ISystem.h"
#include "Assets/MeshManager.h"
#include "Utils/HeightmapLoader.h"

namespace YAEngine
{
  class AssetManager;

  class TerrainSystem : public ISystem
  {
  public:
    explicit TerrainSystem(AssetManager& assets) : m_Assets(assets) {}

    void Update(entt::registry& registry, double dt) override;
    SystemPhase GetPhase() const override { return SystemPhase::Physics; }

  private:
    static constexpr uint32_t DESTROY_DELAY_FRAMES = 3;

    struct PendingDestroy
    {
      MeshHandle handle;
      uint32_t framesLeft;
    };

    struct CachedHeightmap
    {
      std::string path;
      HeightmapData data;
    };

    AssetManager& m_Assets;
    std::vector<PendingDestroy> m_PendingDestroys;
    std::unordered_map<uint32_t, CachedHeightmap> m_HeightmapCache;
  };
}
