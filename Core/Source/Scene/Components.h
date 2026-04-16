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
    float nearPlane = .1f;
    float farPlane = 1000.0f;

    void Resize(float width, float height)
    {
      if (height > 0.0f)
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

  enum class TerrainNoiseType : uint8_t
  {
    FBm,
    Ridged,
    Billowy
  };

  struct TerrainComponent
  {
    float size = 100.0f;
    uint32_t subdivisions = 128;
    float uvScale = 10.0f;
    float heightScale = 10.0f;
    TerrainNoiseType noiseType = TerrainNoiseType::FBm;
    float frequency = 0.02f;
    uint32_t octaves = 4;
    float lacunarity = 2.0f;
    float persistence = 0.5f;
    int32_t seed = 0;
    float warpStrength = 0.0f;
    float warpFrequency = 0.008f;
    std::string heightmapPath;
    std::vector<glm::vec2> maskPath;
    std::vector<glm::vec2> maskCurve;
    float maskFalloffRadius = 0.5f;
  };

  struct TerrainDirty {};

  struct RoadComponent
  {
    std::vector<glm::vec3> points = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 10.0f } };
    float width = 4.0f;
    float uvScale = 1.0f;
    uint32_t segments = 64;
    float carveInnerRadius = 0.0f;
    float carveOuterRadius = 0.0f;
    float carveDepthOffset = 0.0f;
    std::vector<glm::vec2> carveCurve;
  };

  struct RoadDirty {};

  struct TerrainMaterialComponent
  {
    TextureHandle layer1Albedo;
    TextureHandle layer1Normal;
    TextureHandle layer1Roughness;
    TextureHandle layer1Metallic;
    TextureHandle layer2Albedo;
    TextureHandle layer2Normal;
    TextureHandle layer2Roughness;
    TextureHandle layer2Metallic;
    glm::vec3 layer2Tint { 1.0f, 1.0f, 1.0f };
    float layer2RoughnessFactor = 1.0f;
    float layer2MetallicFactor = 0.0f;
    float layer2UvScale = 8.0f;
    float slopeStart = 0.7f;
    float slopeEnd = 0.85f;
    float layer1UvScale = 8.0f;
    float shoulderInnerRadius = 0.0f;
    float shoulderOuterRadius = 0.0f;
    float shoulderWarpAmplitude = 0.0f;
    float shoulderWarpScale = 0.1f;
  };

  enum class ScatterMeshType : uint8_t
  {
    Model,
    Plane
  };

  struct ScatterComponent
  {
    ScatterMeshType meshType = ScatterMeshType::Plane;
    std::string modelPath;
    std::string materialPath;
    uint32_t count = 50;
    int32_t seed = 0;
    float minScale = 0.8f;
    float maxScale = 1.2f;
    float maxSlope = 0.8f;
    bool randomYRotation = true;
    float radius = 0.0f;
    float planeWidth = 1.0f;
    float planeHeight = 1.0f;
    bool useRoadMask = false;
    float roadMaskPadding = 1.0f;
    float roadMaskOuterRadius = 20.0f;
    float roadMaskFalloff = 0.0f;
    std::string clusterSource;
    float clusterRadius = 3.0f;
    uint32_t clusterCountMin = 1;
    uint32_t clusterCountMax = 3;
  };

  struct ScatterDirty {};
  struct ScatterInstanceTag {};

  // opt-out flag for entities created in C++ at runtime that must not be persisted to scene files
  struct NoSerializeTag {};

  enum class ColliderShape : uint8_t
  {
    AABB
  };

  struct ColliderComponent
  {
    ColliderShape shape = ColliderShape::AABB;
    glm::vec3 localOffset { 0.0f };
    glm::vec3 halfExtents { 0.5f };
    bool isStatic = true;
    uint32_t layer = 1;
    // bitmask of layers this collider interacts with; queried as (candidate.layer & query.mask)
    uint32_t mask = ~0u;
  };

  // single entity holds many colliders because ScatterSystem renders clusters as one instanced mesh
  struct InstancedColliderComponent
  {
    struct Entry
    {
      glm::vec3 center;
      glm::vec3 halfExtents;
    };

    ColliderShape shape = ColliderShape::AABB;
    uint32_t layer = 1;
    uint32_t mask = ~0u;
    bool isStatic = true;
    std::vector<Entry> instances;
  };

#ifdef YA_EDITOR
  struct EditorOnlyTag {};
#endif
}
