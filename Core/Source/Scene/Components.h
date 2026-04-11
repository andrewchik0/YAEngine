#pragma once

#include "Assets/MaterialManager.h"
#include "Assets/MeshManager.h"

namespace YAEngine
{
  struct HierarchyComponent
  {
    entt::entity parent { entt::null };
    entt::entity firstChild { entt::null };
    entt::entity nextSibling { entt::null };
  };

  struct LocalTransform
  {
    glm::vec3 position { 0 };
    glm::quat rotation { 1, 0, 0, 0 };
    glm::vec3 scale { 1 };
  };

  struct WorldTransform
  {
    glm::mat4 world { 1.0f };
  };

  struct TransformDirty {};
  struct BoundsDirty {};

  struct LocalBounds
  {
    glm::vec3 min {};
    glm::vec3 max {};
  };

  struct WorldBounds
  {
    glm::vec3 min {};
    glm::vec3 max {};
  };

  struct CameraComponent
  {
    float fov = glm::radians(58.31f);
    float aspectRatio = 16.0f / 9.0f;
    float nearPlane = .01f;
    float farPlane = 1000.0f;

    void Resize(float width, float height)
    {
      aspectRatio = width / height;
    }
  };

  struct MeshComponent
  {
    MeshHandle asset {};
  };

  struct HiddenTag {};


  struct MaterialComponent
  {
    MaterialHandle asset;
  };

  struct RootTag {};

  enum class LightType : uint8_t
  {
    Point,
    Spot,
    Directional
  };

  struct LightComponent
  {
    LightType type = LightType::Point;
    glm::vec3 color { 1.0f };
    float intensity = 1.0f;
    float radius = 10.0f;
    float innerCone = glm::radians(25.0f);
    float outerCone = glm::radians(35.0f);
    bool castShadow = false;
  };

  struct ModelSourceComponent
  {
    std::string path;
    bool combinedTextures = false;
  };

  enum class ProbeShape : uint8_t
  {
    Sphere,
    Box
  };

  struct LightProbeComponent
  {
    ProbeShape shape = ProbeShape::Sphere;
    glm::vec3 extents { 5.0f };
    float fadeDistance = 1.0f;
    int priority = 0;
    uint32_t resolution = 128;
    bool baked = false;
    uint32_t atlasSlot = 0;
    std::string bakedIrradiancePath;
    std::string bakedPrefilterPath;
  };

  struct TerrainComponent
  {
    float size = 100.0f;
    uint32_t subdivisions = 128;
    float uvScale = 10.0f;
    float heightScale = 10.0f;
    float frequency = 0.02f;
    uint32_t octaves = 4;
    float lacunarity = 2.0f;
    float persistence = 0.5f;
    int32_t seed = 0;
  };

  struct TerrainDirty {};

#ifdef YA_EDITOR
  struct EditorOnlyTag {};
#endif
}
