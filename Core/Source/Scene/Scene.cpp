#include "Scene.h"

namespace YAEngine
{

  Entity Scene::CreateEntity(std::string_view name)
  {
    Entity e = m_Registry.create();
    m_Registry.emplace<LocalTransform>(e);
    m_Registry.emplace<WorldTransform>(e);
    m_Registry.emplace<TransformDirty>(e);
    m_Registry.emplace<HierarchyComponent>(e);
    m_Registry.emplace<RootTag>(e);
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
      m_Registry.remove<RootTag>(child);
    }
    else
    {
      if (!m_Registry.all_of<RootTag>(child))
        m_Registry.emplace<RootTag>(child);
    }

    MarkDirty(child);
  }

  LocalTransform& Scene::GetTransform(Entity e)
  {
    return m_Registry.get<LocalTransform>(e);
  }

  const LocalTransform& Scene::GetTransform(Entity e) const
  {
    return m_Registry.get<LocalTransform>(e);
  }

  WorldTransform& Scene::GetWorldTransform(Entity e)
  {
    return m_Registry.get<WorldTransform>(e);
  }

  const WorldTransform& Scene::GetWorldTransform(Entity e) const
  {
    return m_Registry.get<WorldTransform>(e);
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
    if (m_Registry.all_of<TransformDirty>(e))
      return;

    m_Registry.emplace_or_replace<TransformDirty>(e);

    auto& hc = m_Registry.get<HierarchyComponent>(e);
    entt::entity child = hc.firstChild;
    while (child != entt::null)
    {
      MarkDirty(child);
      child = m_Registry.get<HierarchyComponent>(child).nextSibling;
    }
  }


  void Scene::SetDoubleSided(Entity e)
  {
    if (HasComponent<MeshComponent>(e))
      AddComponent<DoubleSidedTag>(e);

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
      AddComponent<NoShadingTag>(e);

    auto& hc = GetHierarchy(e);
    Entity child = hc.firstChild;
    while (child != entt::null)
    {
      NoShading(child);
      child = GetHierarchy(child).nextSibling;
    }
  }

}
