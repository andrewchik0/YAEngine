#pragma once
#include "Layer.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/TerrainSystem.h"
#include "Scene/CollisionQueryService.h"
#include "Assets/Handle.h"
#include "GameComponents.h"
#include "SparkPool.h"

class ControlsLayer : public YAEngine::Layer
{
public:
  struct InputOverride
  {
    bool active = false;
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
  };

  YAEngine::Entity m_Car = entt::null;
  YAEngine::Entity m_Camera = entt::null;
  InputOverride m_InputOverride;
  bool b_StaticLookAt = false;

  // Scenes without a terrain (bistro) pin the car to a constant plane instead of sampling heights
  bool b_FixedGround = false;
  double m_GroundY = 0.0;

  void OnSceneReady() override
  {
    m_Camera = GetScene().CreateEntity("camera");
    GetScene().AddComponent<YAEngine::CameraComponent>(m_Camera);
    GetScene().AddComponent<FollowCameraComponent>(m_Camera);
    GetScene().AddComponent<YAEngine::NoSerializeTag>(m_Camera);

    glm::dvec3 eulerDegrees = glm::vec3(160.0, -0.0, -180.0);
    glm::dvec3 eulerRadians = glm::radians(eulerDegrees);
    GetScene().GetTransform(m_Camera).rotation = glm::quat(eulerRadians);
    GetScene().SetActiveCamera(m_Camera);
  }

  void SetTarget(YAEngine::Entity car)
  {
    m_Car = car;
    if (m_Camera != entt::null)
      GetScene().GetComponent<FollowCameraComponent>(m_Camera).target = car;
  }

  void Update(double deltaTime) override;

private:
  void ResolveAxleGeometry(const glm::dvec3& forwardXZ, const glm::dvec3& carPos);

  YAEngine::TerrainSystem* m_TerrainSystem = nullptr;
  YAEngine::CollisionQueryService* m_CollisionService = nullptr;
  YAEngine::Entity m_TerrainEntity = entt::null;

  // Bicycle model geometry, measured from the wheel entities on the first frame they have
  // a world transform. Defaults cover the frames before that.
  double m_WheelBase = 2.5;
  double m_RearAxleOffset = -1.2;
  bool b_AxleGeometryResolved = false;

  SparkPool m_SparkPool;
  YAEngine::TextureHandle m_SparkTexture;
  std::vector<YAEngine::ParticleInstance> m_SparkInstances;
};
