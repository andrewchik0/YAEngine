#include "Scene.h"

#include "ComponentRegistry.h"

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
    // Copy hierarchy data before destroying children - recursive DestroyEntity
    // calls may invalidate references via entt's swap-and-pop
    auto hc = m_Registry.get<HierarchyComponent>(e);

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

  void Scene::CollectEntityNames(std::unordered_set<Name>& out)
  {
    auto view = m_Registry.view<Name>();
    for (auto e : view)
    {
      // A model node is addressed by its index path inside the override layer, never by
      // name, so repeats among those are expected and harmless
      if (m_Registry.all_of<ModelNodeComponent>(e)
        && m_Registry.get<ModelNodeComponent>(e).nodeIndex != 0)
      {
        continue;
      }

      out.insert(view.get<Name>(e));
    }
  }

  Name Scene::MakeUniqueEntityName(const Name& base, std::unordered_set<Name>& taken)
  {
    Name candidate = base;
    for (uint32_t i = 2; taken.contains(candidate); i++)
      candidate = base + " " + std::to_string(i);

    taken.insert(candidate);
    return candidate;
  }

  Name Scene::MakeUniqueEntityName(const Name& base)
  {
    std::unordered_set<Name> taken;
    CollectEntityNames(taken);
    return MakeUniqueEntityName(base, taken);
  }

  void Scene::CopyEntityData(Entity source, Entity target, const ComponentRegistry& registry)
  {
    m_Registry.get<LocalTransform>(target) = m_Registry.get<LocalTransform>(source);

    // Mesh and material travel as raw handles. Going through the registry serializers would
    // drop a model mesh entirely - they only round-trip primitives - and would hand the copy
    // a private material instead of the shared slot material it is supposed to keep using.
    if (m_Registry.all_of<MeshComponent>(source))
      m_Registry.emplace_or_replace<MeshComponent>(target, m_Registry.get<MeshComponent>(source));

    if (m_Registry.all_of<MaterialComponent>(source))
      m_Registry.emplace_or_replace<MaterialComponent>(target, m_Registry.get<MaterialComponent>(source));

    if (m_Registry.all_of<LocalBounds>(source))
    {
      m_Registry.emplace_or_replace<LocalBounds>(target, m_Registry.get<LocalBounds>(source));
      m_Registry.emplace_or_replace<BoundsDirty>(target);
    }

    auto components = registry.SerializeAll(m_Registry, source);
    for (auto it = components.begin(); it != components.end(); ++it)
    {
      auto key = it->first.as<std::string>();
      if (key == "mesh" || key == "material" || key == "model")
        continue;
      if (it->second.IsNull())
        continue;
      registry.Deserialize(key, m_Registry, target, it->second);
    }

    if (m_Registry.all_of<ModelNodeCloneComponent>(source))
    {
      m_Registry.emplace_or_replace<ModelNodeCloneComponent>(target,
        m_Registry.get<ModelNodeCloneComponent>(source));
    }
    else if (m_Registry.all_of<ModelNodeComponent>(source))
    {
      auto& node = m_Registry.get<ModelNodeComponent>(source);
      // Node 0 is the synthetic model root: ModelBuilder gives it no mesh and no material,
      // so a clone reference to it would have nothing to restore
      if (node.nodeIndex != 0)
      {
        m_Registry.emplace_or_replace<ModelNodeCloneComponent>(target,
          ModelNodeCloneComponent { .modelRoot = node.modelRoot, .nodeIndex = node.nodeIndex });
      }
    }
  }

  Entity Scene::DuplicateInto(Entity source, Entity parent, const ComponentRegistry& registry,
    std::unordered_set<Name>& taken, bool asCopy)
  {
    // Every copy is renamed, not only the top one: the descendants of a model node are model
    // nodes and share the name map with nobody, but their copies are plain entities and would
    // start colliding with each other from the second duplication on
    Name base = m_Registry.get<Name>(source);
    Name name = MakeUniqueEntityName(asCopy ? base + " (Copy)" : base, taken);

    Entity copy = CreateEntity(name);
    CopyEntityData(source, copy, registry);

    if (parent != entt::null)
      SetParent(copy, parent);

    std::vector<Entity> children;
    for (Entity child = m_Registry.get<HierarchyComponent>(source).firstChild; child != entt::null;
      child = m_Registry.get<HierarchyComponent>(child).nextSibling)
    {
      // Runtime-generated children belong to whoever generates them. Copying scatter
      // instances would clone thousands of entities and then fight the regeneration that
      // the copied ScatterComponent kicks off anyway.
      if (m_Registry.all_of<ScatterInstanceTag>(child) || m_Registry.all_of<NoSerializeTag>(child))
        continue;

      children.push_back(child);
    }

    // SetParent prepends, so walking the source children back to front lands the copies in
    // the original order
    for (auto it = children.rbegin(); it != children.rend(); ++it)
      DuplicateInto(*it, copy, registry, taken, false);

    return copy;
  }

  Entity Scene::DuplicateEntity(Entity source, const ComponentRegistry& registry)
  {
    std::unordered_set<Name> taken;
    CollectEntityNames(taken);

    Entity parent = m_Registry.get<HierarchyComponent>(source).parent;
    Entity copy = DuplicateInto(source, parent, registry, taken, true);
    MarkDirty(copy);
    return copy;
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

    // Iterate children only - do not traverse siblings of the root entity
    auto child = hc.firstChild;
    while (child != entt::null)
    {
      auto found = GetChildByName(child, name);
      if (found != entt::null)
        return found;
      child = GetHierarchy(child).nextSibling;
    }

    return entt::null;
  }

  void Scene::MarkDirty(Entity e)
  {
    if (m_Registry.all_of<TransformDirty>(e))
      return;

    m_Registry.emplace_or_replace<TransformDirty>(e);

    auto& hc = m_Registry.get<HierarchyComponent>(e);

    // TransformSystem descends from the roots and stops at the first clean node,
    // so every ancestor has to advertise that there is work below it. The walk
    // stops at the first tagged ancestor: the tag is always laid down all the way
    // to the root, so everything above it already carries it.
    entt::entity ancestor = hc.parent;
    while (ancestor != entt::null && !m_Registry.all_of<DescendantTransformDirty>(ancestor))
    {
      m_Registry.emplace<DescendantTransformDirty>(ancestor);
      ancestor = m_Registry.get<HierarchyComponent>(ancestor).parent;
    }

    entt::entity child = hc.firstChild;
    while (child != entt::null)
    {
      MarkDirty(child);
      child = m_Registry.get<HierarchyComponent>(child).nextSibling;
    }
  }



}
