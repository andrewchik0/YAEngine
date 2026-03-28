#include "TransformSystem.h"

namespace YAEngine
{
  void TransformSystem::Update(entt::registry& registry, double dt)
  {
    auto view = registry.view<RootTag, LocalTransform, WorldTransform, HierarchyComponent>();
    for (auto e : view)
    {
      UpdateWorldTransform(registry, e);
    }
  }

  void TransformSystem::UpdateWorldTransform(entt::registry& registry, entt::entity e)
  {
    auto& lt = registry.get<LocalTransform>(e);
    auto& wt = registry.get<WorldTransform>(e);
    auto& hc = registry.get<HierarchyComponent>(e);

    if (!registry.all_of<TransformDirty>(e))
      return;

    glm::mat4 local = ComposeLocal(lt);

    if (hc.parent != entt::null)
    {
      auto& parentWt = registry.get<WorldTransform>(hc.parent);
      wt.world = parentWt.world * local;
    }
    else
    {
      wt.world = local;
    }

    registry.remove<TransformDirty>(e);

    entt::entity child = hc.firstChild;
    while (child != entt::null)
    {
      if (!registry.all_of<TransformDirty>(child))
        registry.emplace<TransformDirty>(child);
      UpdateWorldTransform(registry, child);
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
