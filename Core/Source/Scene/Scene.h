#pragma once

#include <entt/entt.hpp>

#include "Components.h"
#include "Assets/CubeMapManager.h"
#include "Utils/Log.h"

namespace YAEngine
{
  using Entity = entt::entity;
  using Name = std::string;

  class ComponentRegistry;

  struct EditorCameraState
  {
    glm::vec3 position { 0.0f, 0.0f, 3.0f };
    float yaw = 0.0f;
    float pitch = 0.0f;
  };

  class Scene
  {
  public:

    [[nodiscard]] Entity CreateEntity(std::string_view name);
    void DestroyEntity(Entity e);
    void SetParent(Entity child, Entity parent);

    // Deep copy of an entity and its subtree, placed next to the source. Copies are plain
    // entities: a nested model root loses its ModelSourceComponent, since a Model asset
    // points back at exactly one root entity and cannot be shared by two.
    Entity DuplicateEntity(Entity source, const ComponentRegistry& registry);

    // The scene file addresses every entity that is not a model node by name, and resolves
    // parent references through one flat name map - so a repeat there silently reparents
    // somebody on load. Returns base, or base with a numeric suffix.
    Name MakeUniqueEntityName(const Name& base);

    LocalTransform& GetTransform(Entity e);
    const LocalTransform& GetTransform(Entity e) const;
    WorldTransform& GetWorldTransform(Entity e);
    const WorldTransform& GetWorldTransform(Entity e) const;
    HierarchyComponent& GetHierarchy(Entity e);
    const HierarchyComponent& GetHierarchy(Entity e) const;
    Name& GetName(Entity e);
    const Name& GetName(Entity e) const;

    // For empty tag types (e.g. EditorOnlyTag) returns void; for data components returns T&
    template<typename T, typename... Args>
    decltype(auto) AddComponent(Entity e, Args&&... args)
    {
      if constexpr (std::is_empty_v<T>)
      {
        if (!m_Registry.all_of<T>(e))
          m_Registry.emplace<T>(e, std::forward<Args>(args)...);
      }
      else
      {
        if (!m_Registry.all_of<T>(e))
          return m_Registry.emplace<T>(e, std::forward<Args>(args)...);
        return m_Registry.get<T>(e);
      }
    }

    template<typename T>
    void RemoveComponent(Entity e)
    {
      if (m_Registry.all_of<T>(e))
        m_Registry.remove<T>(e);
    }

    template<typename T>
    bool HasComponent(Entity e) const
    {
      return m_Registry.all_of<T>(e);
    }

    template<typename T>
    T& GetComponent(Entity e)
    {
      if (m_Registry.all_of<T>(e))
        return m_Registry.get<T>(e);

      YA_LOG_ERROR("Scene", "Entity does not have requested component");
      throw std::runtime_error("Entity does not have requested component");
    }

    template<typename T>
    const T& GetComponent(Entity e) const
    {
      if (m_Registry.all_of<T>(e))
        return m_Registry.get<T>(e);

      YA_LOG_ERROR("Scene", "Entity does not have requested component");
      throw std::runtime_error("Entity does not have requested component");
    }

    Entity GetChildByName(Entity entity, const Name& name) const;

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

    void SetSkybox(CubeMapHandle handle)
    {
      m_Skybox = handle;
    }

    CubeMapHandle GetSkybox() const
    {
      return m_Skybox;
    }

    void SetScenePath(const std::string& path) { m_ScenePath = path; }
    const std::string& GetScenePath() const { return m_ScenePath; }

    void ClearScene()
    {
      m_Registry.clear();
      m_ActiveCamera = entt::null;
      m_Skybox = {};
      m_ScenePath.clear();
      m_EditorCameraState = {};
    }

    entt::registry& GetRegistry() { return m_Registry; }

    EditorCameraState& GetEditorCameraState() { return m_EditorCameraState; }
    const EditorCameraState& GetEditorCameraState() const { return m_EditorCameraState; }

  private:

    Entity DuplicateInto(Entity source, Entity parent, const ComponentRegistry& registry,
      std::unordered_set<Name>& taken, bool asCopy);
    void CopyEntityData(Entity source, Entity target, const ComponentRegistry& registry);
    void CollectEntityNames(std::unordered_set<Name>& out);
    Name MakeUniqueEntityName(const Name& base, std::unordered_set<Name>& taken);

    entt::registry m_Registry;

    entt::entity m_ActiveCamera = entt::null;
    CubeMapHandle m_Skybox {};
    std::string m_ScenePath;
    EditorCameraState m_EditorCameraState;

  };

}
