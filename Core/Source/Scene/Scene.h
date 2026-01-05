#pragma once

#include <entt/entt.hpp>

#include "Components.h"

namespace YAEngine
{
  using Entity = entt::entity;
  using Name = std::string;

  class Scene
  {
  public:

    [[nodiscard]] Entity CreateEntity(std::string_view name);
    void SetParent(Entity child, Entity parent);

    TransformComponent& GetTransform(Entity e);

    template<typename T, typename... Args>
    T& AddComponent(Entity e, Args&&... args)
    {
      if (!m_Registry.all_of<T>(e))
      {
        return m_Registry.emplace<T>(e, std::forward<Args>(args)...);
      }
      return m_Registry.get<T>(e);
    }

    template<typename T>
    void RemoveComponent(Entity e)
    {
      if (m_Registry.all_of<T>(e))
        m_Registry.remove<T>(e);
    }

    template<typename T>
    bool HasComponent(Entity e)
    {
      return m_Registry.all_of<T>(e);
    }

    template<typename T>
    T& GetComponent(Entity e)
    {
      if (m_Registry.all_of<T>(e))
        return m_Registry.get<T>(e);

      throw std::runtime_error("Entity does not have requested component");
    }

    template<typename... Ts>
    auto GetView()
    {
      return m_Registry.view<Ts...>();
    }

    void SetActiveCamera(Entity e)
    {
      m_ActiveCamera = e;
    }

    Entity& GetActiveCamera()
    {
      return m_ActiveCamera;
    }

    void MarkDirty(Entity e);
    void Update();
    void UpdateWorldTransform(entt::entity e);

  private:
    entt::registry m_Registry;

    entt::entity m_ActiveCamera = entt::null;

    glm::mat4 ComposeLocal(const TransformComponent& t);

    friend class Render;
  };

}
