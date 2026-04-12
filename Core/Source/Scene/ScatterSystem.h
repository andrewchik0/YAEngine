#pragma once

#include <entt/entt.hpp>
#include "Scene/ISystem.h"
#include "Assets/MeshManager.h"

namespace YAEngine
{
  class AssetManager;
  class Render;
  class Scene;

  class ScatterSystem : public ISystem
  {
  public:
    ScatterSystem(AssetManager& assets, Scene& scene, Render& render)
      : m_Assets(assets), m_Scene(scene), m_Render(render) {}

    void Update(entt::registry& registry, double dt) override;
    SystemPhase GetPhase() const override { return SystemPhase::Physics; }
    int GetPriority() const override { return 1; }

  private:
    static constexpr uint32_t DESTROY_DELAY_FRAMES = 3;

    struct PendingDestroy
    {
      MeshHandle mesh;
      MaterialHandle material;
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
    std::vector<PendingDestroy> m_PendingDestroys;
    std::unordered_map<uint32_t, ScatterState> m_States;

    void DestroyState(uint32_t entityId);
    void GenerateScatter(entt::registry& registry, entt::entity entity);
  };
}
