#pragma once

#include "Assets/MaterialManager.h"
#include "Assets/MeshManager.h"

namespace YAEngine
{
  struct TransformComponent
  {
    glm::vec3 position { 0 };
    glm::quat rotation { 1, 0, 0, 0 };
    glm::vec3 scale { 1 };

    entt::entity parent { entt::null };
    entt::entity firstChild { entt::null };
    entt::entity nextSibling { entt::null };

    glm::mat4 local { 1.0f };
    glm::mat4 world { 1.0f };

    bool dirty = true;
  };

  struct CameraComponent
  {
    float fov = 45.0f;
    float aspectRatio = 16.0f / 9.0f;
    float nearPlane = 0.01f;
    float farPlane = 1000.0f;

    void Resize(float width, float height)
    {
      aspectRatio = width / height;
    }
  };

  struct MeshComponent
  {
    MeshHandle asset;
    bool shouldRender = true;
    bool doubleSided = false;
  };


  struct MaterialComponent
  {
    MaterialHandle asset;
  };
}
