#pragma once
#include "Layer.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/TerrainSystem.h"
#include "Scene/CollisionQueryService.h"
#include "Scene/SequencePlayer.h"
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
  struct BodyPart
  {
    YAEngine::Entity entity = entt::null;
    YAEngine::LocalTransform base;
  };

  struct WheelSpin
  {
    YAEngine::Entity entity = entt::null;
    double spinAngle = 0.0;
  };

  void ResolveAxleGeometry(const glm::dvec3& forwardXZ, const glm::dvec3& carPos);

  // Poses the car from its motion path at the timeline time; false when this frame belongs to
  // the input drive instead
  bool DriveAlongPath(double dt);
  void ReleasePathDrive();
  void CaptureBody(YAEngine::SequencePlayer& player);
  void ApplyBodyLean(const YAEngine::MotionPathPose& pose);

  glm::dvec3 SnapToGround(glm::dvec3 p) const;
  glm::dquat TerrainTilt(const glm::dvec3& position) const;
  void UpdateFollowCamera(double dt, const glm::dvec3& position, const glm::dquat& yawRot,
    const VehicleComponent& vehicle);
  void UpdateWheels(double dt, const VehicleComponent& vehicle, double pathDistance, bool fromPath);
  void UpdateSparks(double dt);

  YAEngine::TerrainSystem* m_TerrainSystem = nullptr;
  YAEngine::CollisionQueryService* m_CollisionService = nullptr;
  YAEngine::Entity m_TerrainEntity = entt::null;

  // Bicycle model geometry, measured from the wheel entities on the first frame they have
  // a world transform. Defaults cover the frames before that.
  double m_WheelBase = 2.5;
  double m_RearAxleOffset = -1.2;
  bool b_AxleGeometryResolved = false;

  // Path drive state, captured on its first frame and dropped when the session ends
  bool b_PathDriven = false;
  bool b_BodyCaptured = false;
  std::vector<BodyPart> m_BodyParts;
  std::vector<WheelSpin> m_WheelSpins;
  // Rotation of the body parts' parent relative to the car root, and the axle centre in that
  // parent's space
  glm::quat m_BodyFrame { 1.0f, 0.0f, 0.0f, 0.0f };
  glm::vec3 m_BodyPivot { 0.0f };

  SparkPool m_SparkPool;
  YAEngine::TextureHandle m_SparkTexture;
  std::vector<YAEngine::ParticleInstance> m_SparkInstances;
};
