#pragma once

#include <entt/entt.hpp>
#include "Scene/ISystem.h"
#include "Assets/MeshManager.h"
#include "Utils/HeightmapLoader.h"

namespace YAEngine
{
  class AssetManager;

  struct TerrainHeightGrid
  {
    std::vector<float> heights;
    uint32_t vertsPerSide = 0;
    float size = 0.0f;
  };

  class TerrainSystem : public ISystem
  {
  public:
    explicit TerrainSystem(AssetManager& assets) : m_Assets(assets) {}

    void Update(entt::registry& registry, double dt) override;
    void OnSceneClear() override;
    SystemPhase GetPhase() const override { return SystemPhase::Physics; }

    float SampleCachedHeight(uint32_t entityId, float worldX, float worldZ) const;
    bool HasCachedHeight(uint32_t entityId) const;

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
    std::unordered_map<uint32_t, TerrainHeightGrid> m_HeightGridCache;
  };
}
