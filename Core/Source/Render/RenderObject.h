#pragma once

#include "Pch.h"
#include "Assets/Handle.h"
#include "ReflectionProbeData.h"
#include "Utils/IrradianceGrid.h"

namespace YAEngine
{
  struct RenderObject
  {
    MeshHandle mesh;
    MaterialHandle material;
    glm::mat4 worldTransform;
    glm::vec3 boundsMin { std::numeric_limits<float>::max() };
    glm::vec3 boundsMax { std::numeric_limits<float>::lowest() };
    std::vector<glm::mat4>* instanceData = nullptr;
    uint32_t instanceOffset = 0;
    bool doubleSided = false;
    bool noShading = false;
    bool isTerrain = false;
    bool isAlphaTest = false;
    bool isTransparent = false;
#ifdef YA_EDITOR
    // Raw entt handle of the owning entity, written into the pick buffer by the editor's
    // ID pass. Stored as a plain integer so the render layer stays free of entt.
    uint32_t entityId = 0;
#endif
  };

  struct CameraData
  {
    glm::vec3 position { 0.0f };
    glm::quat rotation { 1, 0, 0, 0 };
    float fov = glm::radians(58.31f);
    float aspectRatio = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
  };

  struct DirectionalShadowData
  {
    glm::vec3 direction { 0.0f, -1.0f, 0.0f };
    glm::vec3 position { 0.0f };
    float shadowDistance = 200.0f;
    bool castShadow = false;
  };

  struct SpotShadowRequest
  {
    glm::vec3 position { 0.0f };
    glm::vec3 direction { 0.0f, 0.0f, -1.0f };
    float outerCone = 0.0f;
    float radius = 0.0f;
    uint32_t lightIndex = 0;
  };

  struct PointShadowRequest
  {
    glm::vec3 position { 0.0f };
    float radius = 0.0f;
    uint32_t lightIndex = 0;
  };

  struct TerrainMaterialComponent;

  struct TerrainRenderData
  {
    MaterialHandle layer0;
    const TerrainMaterialComponent* layer1 = nullptr;
    std::vector<glm::vec2> roadPolyline;
  };

  // One irradiance volume as the renderer sees it. Carries the full box
  // description because the same data fills the volume UBO.
  struct IrradianceVolumeInstance
  {
    glm::vec3 center { 0.0f };
    glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };
    IrradianceGridLayout grid;
  };

  // Union of a moved shadow caster's previous and current world AABB - the
  // region of a shadow tile its movement can have changed.
  struct ShadowMoverBounds
  {
    glm::vec3 min { 0.0f };
    glm::vec3 max { 0.0f };
  };

  struct SceneSnapshot
  {
    std::vector<RenderObject> objects;
    uint32_t visibleCount = 0;
    CameraData camera;
    CubeMapHandle skybox;
    DirectionalShadowData directionalShadow;
    std::vector<SpotShadowRequest> spotShadowRequests;
    std::vector<PointShadowRequest> pointShadowRequests;
    ReflectionProbeBuffer probeBuffer {};
#ifdef YA_EDITOR
    // Only the volume bounds gizmo reads this - the shader gets its volume data
    // from IrradianceVolumeStorage, which is filled at upload time.
    std::vector<IrradianceVolumeInstance> irradianceVolumes;
#endif
    TerrainRenderData terrainData {};

    // Shadow cache keys, folded by BuildSceneSnapshot in registry order - the
    // later frustum-cull partition of `objects` does not touch them, so camera
    // motion alone never flips a digest. Equal digests mean the shadow pass
    // would redraw the atlas with identical content.
    uint64_t casterIdentityDigest = 0;
    uint64_t casterTransformDigest = 0;
    uint64_t lightDigest = 0;

    // Stage 6 dirty rects: union AABBs of the shadow-relevant casters that
    // moved in the tick right before this snapshot. The transform digest
    // stays the trigger; these attribute the change to footprints. A mover
    // without WorldBounds cannot be attributed one and sets the flag, which
    // forces the full-rebuild fallback.
    std::vector<ShadowMoverBounds> shadowMoverBounds;
    bool shadowMoverUnbounded = false;
  };
}
