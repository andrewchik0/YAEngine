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

  void Scene::MarkDirty(Entity e)
  {
    auto& t = m_Registry.get<TransformComponent>(e);

    if (t.parent != entt::null)
    {
      entt::entity parent = t.parent;
      while (parent != entt::null)
      {
        m_Registry.get<TransformComponent>(parent).dirty = true;
        parent = m_Registry.get<TransformComponent>(parent).parent;
      }
    }

    if (t.dirty)
      return;

    t.dirty = true;

    entt::entity child = t.firstChild;
    while (child != entt::null)
    {
      MarkDirty(child);
      child = m_Registry.get<TransformComponent>(child).nextSibling;
    }
  }

  void Scene::Update()
  {
    auto view = m_Registry.view<TransformComponent>();

    for (auto e : view)
    {
      auto& t = view.get<TransformComponent>(e);
      if (t.parent == entt::null)
      {
        UpdateWorldTransform(e);
      }
    }
  }

  void Scene::UpdateWorldTransform(Entity e)
  {
    auto& t = m_Registry.get<TransformComponent>(e);

    if (!t.dirty)
      return;

    t.local = ComposeLocal(t);

    if (t.parent != entt::null)
    {
      auto& parent = m_Registry.get<TransformComponent>(t.parent);
      t.world = parent.world * t.local;
    }
    else
    {
      t.world = t.local;
    }

    t.dirty = false;

    entt::entity child = t.firstChild;
    while (child != entt::null)
    {
      auto& ct = m_Registry.get<TransformComponent>(child);
      ct.dirty = true;
      UpdateWorldTransform(child);
      child = ct.nextSibling;
    }
  }

  glm::mat4 Scene::ComposeLocal(const TransformComponent& t)
  {
    glm::mat4 T = glm::translate(glm::mat4(1.0f), t.position);
    glm::mat4 R = glm::toMat4(t.rotation);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), t.scale);
    return T * R * S;
  }

  void Scene::SetDoubleSided(Entity e)
  {
    if (HasComponent<MeshComponent>(e))
    {
      GetComponent<MeshComponent>(e).doubleSided = true;
    }

    auto next = GetTransform(e).nextSibling;
    while (next != entt::null)
    {
      if (HasComponent<MeshComponent>(next))
      {
        GetComponent<MeshComponent>(next).doubleSided = true;
      }
      next = GetTransform(next).nextSibling;
    }

    if (GetTransform(e).firstChild != entt::null)
    {
      SetDoubleSided(GetTransform(e).firstChild);
    }
  }

}
