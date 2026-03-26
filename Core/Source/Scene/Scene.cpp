#include "Scene.h"
#include "TransformSystem.h"

namespace YAEngine
{

  Entity Scene::CreateEntity(std::string_view name)
  {
    Entity e = m_Registry.create();
    m_Registry.emplace<TransformComponent>(e);
    m_Registry.emplace<HierarchyComponent>(e);
    m_Registry.emplace<Name>(e, name);
    return e;
  }

  void Scene::DestroyEntity(Entity e)
  {
    auto& hc = m_Registry.get<HierarchyComponent>(e);

    Entity child = hc.firstChild;
    while (child != entt::null)
    {
      Entity next = m_Registry.get<HierarchyComponent>(child).nextSibling;
      DestroyEntity(child);
      child = next;
    }

    if (hc.parent != entt::null)
    {
      auto& parentHc = m_Registry.get<HierarchyComponent>(hc.parent);
      entt::entity* link = &parentHc.firstChild;
      while (*link != entt::null)
      {
        if (*link == e)
        {
          *link = hc.nextSibling;
          break;
        }
        link = &m_Registry.get<HierarchyComponent>(*link).nextSibling;
      }
    }

    m_Registry.destroy(e);
  }

  void Scene::SetParent(Entity child, Entity parent)
  {
    auto& childH = m_Registry.get<HierarchyComponent>(child);

    // Cycle detection: walk up from parent to root, reject if child is found
    if (parent != entt::null)
    {
      entt::entity ancestor = parent;
      while (ancestor != entt::null)
      {
        if (ancestor == child)
          return;
        ancestor = m_Registry.get<HierarchyComponent>(ancestor).parent;
      }
    }

    // Unlink from old parent
    if (childH.parent != entt::null)
    {
      auto& oldParent = m_Registry.get<HierarchyComponent>(childH.parent);

      entt::entity* link = &oldParent.firstChild;
      while (*link != entt::null)
      {
        if (*link == child)
        {
          *link = m_Registry.get<HierarchyComponent>(*link).nextSibling;
          break;
        }
        link = &m_Registry.get<HierarchyComponent>(*link).nextSibling;
      }
    }

    childH.parent = parent;
    childH.nextSibling = entt::null;

    if (parent != entt::null)
    {
      auto& parentH = m_Registry.get<HierarchyComponent>(parent);
      childH.nextSibling = parentH.firstChild;
      parentH.firstChild = child;
    }

    m_Registry.get<TransformComponent>(child).dirty = true;
  }

  TransformComponent& Scene::GetTransform(Entity e)
  {
    return m_Registry.get<TransformComponent>(e);
  }

  const TransformComponent& Scene::GetTransform(Entity e) const
  {
    return m_Registry.get<TransformComponent>(e);
  }

  HierarchyComponent& Scene::GetHierarchy(Entity e)
  {
    return m_Registry.get<HierarchyComponent>(e);
  }

  const HierarchyComponent& Scene::GetHierarchy(Entity e) const
  {
    return m_Registry.get<HierarchyComponent>(e);
  }

  Name& Scene::GetName(Entity e)
  {
    return m_Registry.get<Name>(e);
  }

  const Name& Scene::GetName(Entity e) const
  {
    return m_Registry.get<Name>(e);
  }

  Entity Scene::GetChildByName(Entity entity, const Name& name) const
  {
    const auto& currentEntityName = GetName(entity);
    if (currentEntityName == name)
      return entity;

    const auto& hc = GetHierarchy(entity);

    auto firstChild = hc.firstChild;
    if (firstChild != entt::null)
    {
      auto child = GetChildByName(firstChild, name);
      if (child != entt::null)
        return child;
    }

    auto sibling = hc.nextSibling;
    while (sibling != entt::null)
    {
      auto child = GetChildByName(sibling, name);

      if (child != entt::null)
      {
        const auto& childName = GetName(child);
        if (childName == name)
          return child;
      }
      sibling = GetHierarchy(sibling).nextSibling;
    }

    return entt::null;
  }

  void Scene::MarkDirty(Entity e)
  {
    auto& t = m_Registry.get<TransformComponent>(e);
    auto& hc = m_Registry.get<HierarchyComponent>(e);

    if (hc.parent != entt::null)
    {
      entt::entity parent = hc.parent;
      while (parent != entt::null)
      {
        m_Registry.get<TransformComponent>(parent).dirty = true;
        parent = m_Registry.get<HierarchyComponent>(parent).parent;
      }
    }

    if (t.dirty)
      return;

    t.dirty = true;

    entt::entity child = hc.firstChild;
    while (child != entt::null)
    {
      MarkDirty(child);
      child = m_Registry.get<HierarchyComponent>(child).nextSibling;
    }
  }

  void Scene::Update()
  {
    TransformSystem::Update(m_Registry);
  }

  void Scene::SetDoubleSided(Entity e)
  {
    if (HasComponent<MeshComponent>(e))
    {
      GetComponent<MeshComponent>(e).doubleSided = true;
    }

    auto& hc = GetHierarchy(e);
    Entity child = hc.firstChild;
    while (child != entt::null)
    {
      SetDoubleSided(child);
      child = GetHierarchy(child).nextSibling;
    }
  }

  void Scene::NoShading(Entity e)
  {
    if (HasComponent<MeshComponent>(e))
    {
      GetComponent<MeshComponent>(e).noShading = true;
    }

    auto& hc = GetHierarchy(e);
    Entity child = hc.firstChild;
    while (child != entt::null)
    {
      NoShading(child);
      child = GetHierarchy(child).nextSibling;
    }
  }

}
