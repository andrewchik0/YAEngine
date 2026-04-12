#pragma once

#include <entt/entt.hpp>
#include "Scene/ISystem.h"
#include "Assets/MeshManager.h"
#include "Utils/HeightmapLoader.h"
#include "Utils/PrimitiveMeshFactory.h"

namespace YAEngine
{
  class AssetManager;
  class ThreadPool;

  struct TerrainHeightGrid
  {
    std::vector<float> heights;
    uint32_t vertsPerSide = 0;
    float size = 0.0f;
  };

  struct TerrainGenResult
  {
    ProcMesh mesh;
    TerrainHeightGrid heightGrid;
    HeightmapData loadedHeightmap;
    std::string heightmapPath;
    bool hasNewHeightmap = false;
  };

  class TerrainSystem : public ISystem
  {
  public:
    explicit TerrainSystem(AssetManager& assets, ThreadPool* threadPool = nullptr)
      : m_Assets(assets), m_ThreadPool(threadPool) {}

    void Update(entt::registry& registry, double dt) override;
    void OnSceneClear() override;
    SystemPhase GetPhase() const override { return SystemPhase::Physics; }

    float SampleCachedHeight(uint32_t entityId, float worldX, float worldZ) const;
    bool HasCachedHeight(uint32_t entityId) const;
    const TerrainHeightGrid* GetCachedHeightGrid(uint32_t entityId) const;

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
    ThreadPool* m_ThreadPool = nullptr;
    std::vector<PendingDestroy> m_PendingDestroys;
    std::unordered_map<uint32_t, CachedHeightmap> m_HeightmapCache;
    std::unordered_map<uint32_t, TerrainHeightGrid> m_HeightGridCache;

    void FinalizeTerrain(entt::registry& registry, entt::entity entity,
                         TerrainGenResult&& result, MeshHandle oldMesh, bool hadOldMesh);
  };
}
