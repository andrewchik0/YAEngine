#include "Scene.h"

namespace YAEngine
{

  Entity Scene::CreateEntity(std::string_view name)
  {
    Entity e = m_Registry.create();
    m_Registry.emplace<TransformComponent>(e);
    if (!name.empty())
      m_Registry.emplace<Name>(e, name);
    return e;
  }

  void Scene::SetParent(Entity child, Entity parent)
  {
    auto& childT = m_Registry.get<TransformComponent>(child);

    if (childT.parent != entt::null)
    {
      auto& oldParent = m_Registry.get<TransformComponent>(childT.parent);

      entt::entity* link = &oldParent.firstChild;
      while (*link != entt::null)
      {
        if (*link == child)
        {
          *link = m_Registry.get<TransformComponent>(*link).nextSibling;
          break;
        }
        link = &m_Registry.get<TransformComponent>(*link).nextSibling;
      }
    }

    childT.parent = parent;
    childT.nextSibling = entt::null;

    if (parent != entt::null)
    {
      auto& parentT = m_Registry.get<TransformComponent>(parent);
      childT.nextSibling = parentT.firstChild;
      parentT.firstChild = child;
    }

    childT.dirty = true;
  }

  TransformComponent& Scene::GetTransform(Entity e)
  {
    return m_Registry.get<TransformComponent>(e);
  }
}