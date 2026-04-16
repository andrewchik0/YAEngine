#pragma once

#include <entt/entt.hpp>
#include "Scene/ISystem.h"
#include "Assets/MeshManager.h"
#include "Assets/ModelDescription.h"

namespace YAEngine
{
  class AssetManager;
  class Render;
  class Scene;
  class TerrainSystem;
  class ThreadPool;

  class ScatterSystem : public ISystem
  {
  public:
    ScatterSystem(AssetManager& assets, Scene& scene, Render& render,
                  TerrainSystem& terrainSystem, ThreadPool* threadPool = nullptr)
      : m_Assets(assets), m_Scene(scene), m_Render(render),
        m_TerrainSystem(terrainSystem), m_ThreadPool(threadPool) {}

    void Update(entt::registry& registry, double dt) override;
    void OnSceneClear() override;
    SystemPhase GetPhase() const override { return SystemPhase::Physics; }
    int GetPriority() const override { return 1; }

  private:
    static constexpr uint32_t DESTROY_DELAY_FRAMES = 3;

    struct PendingDestroy
    {
      MeshHandle mesh;
      MaterialHandle material;
      uint32_t instanceOffset;
      uint32_t instanceSize;
      uint32_t framesLeft;
    };

    struct ScatterState
    {
      entt::entity childEntity = entt::null;
      MeshHandle mesh;
      MaterialHandle material;
      std::vector<glm::mat4> instanceMatrices;
      uint32_t instanceOffset = 0;
    };

    AssetManager& m_Assets;
    Scene& m_Scene;
    Render& m_Render;
    TerrainSystem& m_TerrainSystem;
    ThreadPool* m_ThreadPool = nullptr;
    std::vector<PendingDestroy> m_PendingDestroys;
    std::unordered_map<uint32_t, ScatterState> m_States;
    std::unordered_map<std::string, ModelDescription> m_ModelCache;

    void DestroyState(uint32_t entityId);
    void GenerateScatter(entt::registry& registry, entt::entity entity);
    void GenerateSatelliteScatter(entt::registry& registry, entt::entity entity);
    entt::entity FindEntityByName(entt::registry& registry, const std::string& name) const;
  };
}
