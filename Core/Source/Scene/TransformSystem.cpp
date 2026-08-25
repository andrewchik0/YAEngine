#include "TransformSystem.h"

namespace YAEngine
{
  void TransformSystem::Update(entt::registry& registry, double dt)
  {
    m_Tick++;
    registry.ctx().emplace<TransformTickContext>().tick = m_Tick;
    auto view = registry.view<RootTag, LocalTransform, WorldTransform, HierarchyComponent>();
    for (auto e : view)
    {
      UpdateWorldTransform(registry, e, m_Tick);
    }
  }

  void TransformSystem::UpdateWorldTransform(entt::registry& registry, entt::entity e, uint64_t tick)
  {
    auto& lt = registry.get<LocalTransform>(e);
    auto& wt = registry.get<WorldTransform>(e);
    auto& hc = registry.get<HierarchyComponent>(e);

    if (!registry.all_of<TransformDirty>(e))
      return;

    glm::mat4 local = ComposeLocal(lt);

    glm::mat4 world;
    if (hc.parent != entt::null)
    {
      auto& parentWt = registry.get<WorldTransform>(hc.parent);
      world = parentWt.world * local;
    }
    else
    {
      world = local;
    }

    // Redundant MarkDirty writers (a controller rewriting an unchanged transform
    // every frame) must not churn the shadow cache's transform digest. The compare
    // is exact on purpose: identical inputs compose bitwise identical products, so
    // a stationary writer converges instead of stamping every frame.
    if (world != wt.world)
    {
      wt.world = world;
      wt.lastChangeTick = tick;

      // Bounds cannot have changed if the world matrix did not.
      if (registry.all_of<LocalBounds>(e) && !registry.all_of<BoundsDirty>(e))
        registry.emplace<BoundsDirty>(e);
    }

    registry.remove<TransformDirty>(e);

    entt::entity child = hc.firstChild;
    while (child != entt::null)
    {
      if (!registry.all_of<TransformDirty>(child))
        registry.emplace<TransformDirty>(child);
      UpdateWorldTransform(registry, child, tick);
      child = registry.get<HierarchyComponent>(child).nextSibling;
    }
  }

  glm::mat4 TransformSystem::ComposeLocal(const LocalTransform& t)
  {
    glm::mat4 T = glm::translate(glm::mat4(1.0f), t.position);
    glm::mat4 R = glm::toMat4(t.rotation);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), t.scale);
    return T * R * S;
  }
}
