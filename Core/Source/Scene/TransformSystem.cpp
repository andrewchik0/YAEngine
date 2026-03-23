#include "TransformSystem.h"

namespace YAEngine
{
  void TransformSystem::Update(entt::registry& registry)
  {
    auto view = registry.view<TransformComponent, HierarchyComponent>();

    for (auto e : view)
    {
      auto& hc = view.get<HierarchyComponent>(e);
      if (hc.parent == entt::null)
      {
        UpdateWorldTransform(registry, e);
      }
    }
  }

  void TransformSystem::UpdateWorldTransform(entt::registry& registry, entt::entity e)
  {
    auto& t = registry.get<TransformComponent>(e);
    auto& hc = registry.get<HierarchyComponent>(e);

    if (!t.dirty)
      return;

    t.local = ComposeLocal(t);

    if (hc.parent != entt::null)
    {
      auto& parentT = registry.get<TransformComponent>(hc.parent);
      t.world = parentT.world * t.local;
    }
    else
    {
      t.world = t.local;
    }

    t.dirty = false;

    entt::entity child = hc.firstChild;
    while (child != entt::null)
    {
      auto& ct = registry.get<TransformComponent>(child);
      ct.dirty = true;
      UpdateWorldTransform(registry, child);
      child = registry.get<HierarchyComponent>(child).nextSibling;
    }
  }

  glm::mat4 TransformSystem::ComposeLocal(const TransformComponent& t)
  {
    glm::mat4 T = glm::translate(glm::mat4(1.0f), t.position);
    glm::mat4 R = glm::toMat4(t.rotation);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), t.scale);
    return T * R * S;
  }
}
