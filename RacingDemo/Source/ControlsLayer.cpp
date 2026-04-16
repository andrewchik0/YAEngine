#include "ControlsLayer.h"
#include "Input/InputSystem.h"
#include "Utils/Log.h"

void ControlsLayer::Update(double dt)
{
  if (m_Car == entt::null) return;
  if (!GetScene().HasComponent<VehicleComponent>(m_Car)) return;

  if (m_TerrainSystem == nullptr)
    m_TerrainSystem = &m_Registry->Get<YAEngine::TerrainSystem>();
  if (m_CollisionService == nullptr)
    m_CollisionService = &m_Registry->Get<YAEngine::CollisionQueryService>();
  if (m_TerrainEntity == entt::null)
  {
    auto terrainView = GetScene().GetView<YAEngine::TerrainComponent>();
    for (auto te : terrainView)
    {
      m_TerrainEntity = te;
      YA_LOG_INFO("Physics", "Terrain resolved for snap: entity=%u",
        static_cast<uint32_t>(m_TerrainEntity));
      break;
    }
  }

  auto& input = GetInput();

  auto car = m_Car;
  auto* vehicle = &GetScene().GetComponent<VehicleComponent>(car);

  glm::dvec3 position = GetScene().GetTransform(car).position;
  glm::dquat rotation = GetScene().GetTransform(car).rotation;

  bool arrowLeft  = input.IsKeyDown(YAEngine::Key::Left);
  bool arrowRight = input.IsKeyDown(YAEngine::Key::Right);
  bool arrowUp    = input.IsKeyDown(YAEngine::Key::Up);
  bool arrowDown  = input.IsKeyDown(YAEngine::Key::Down);

  if (arrowLeft)
  {
    vehicle->wheelsSteer = glm::clamp(vehicle->wheelsSteer + 0.1 * dt, 0.0, 0.03);
  }
  if (arrowRight)
  {
    vehicle->wheelsSteer = glm::clamp(vehicle->wheelsSteer - 0.1 * dt, -0.03, 0.0);
  }
  if (!arrowLeft && !arrowRight)
  {
    if (vehicle->wheelsSteer > 0.0) vehicle->wheelsSteer = glm::clamp(vehicle->wheelsSteer - 0.2 * dt, 0.0, 0.03);
    if (vehicle->wheelsSteer < 0.0) vehicle->wheelsSteer = glm::clamp(vehicle->wheelsSteer + 0.2 * dt, -0.03, 0.0);
  }

  if (arrowUp)
    vehicle->speed += vehicle->acceleration * dt;
  else if (arrowDown)
  {
    if (vehicle->speed > 0)
      vehicle->speed -= vehicle->brake * dt;
    else
      vehicle->speed -= vehicle->accelerationBack * dt;
  }
  else
    vehicle->speed -= vehicle->drag * dt * (vehicle->speed > 0 ? 1 : vehicle->speed < 0 ? -1 : 0);

  if (vehicle->speed < 0.001 && vehicle->speed > -0.001) vehicle->speed = 0;

  vehicle->speed = glm::clamp(vehicle->speed, -vehicle->maxSpeedBack, vehicle->maxSpeed);

  double turnSpeed = glm::radians(260.0 - vehicle->speed / vehicle->maxSpeed * 160.0);
  double turn = vehicle->wheelsSteer * (1.0 / 0.03) * turnSpeed * dt * (vehicle->speed / vehicle->maxSpeed);

  glm::dquat rotDelta = glm::angleAxis(turn, glm::dvec3(0,1,0));
  rotation = rotDelta * rotation;

  glm::dvec3 forward = rotation * glm::dvec3(0, 0, 1);
  glm::dvec3 prevPosition = position;
  position += forward * vehicle->speed * dt;

  if (m_TerrainEntity != entt::null
    && m_TerrainSystem->HasCachedHeight(static_cast<uint32_t>(m_TerrainEntity)))
  {
    position.y = static_cast<double>(m_TerrainSystem->SampleCachedHeight(
      static_cast<uint32_t>(m_TerrainEntity),
      static_cast<float>(position.x),
      static_cast<float>(position.z)));
  }

  if (GetScene().HasComponent<YAEngine::ColliderComponent>(car))
  {
    auto& collider = GetScene().GetComponent<YAEngine::ColliderComponent>(car);
    glm::quat orientation = glm::quat(rotation);
    glm::vec3 center = glm::vec3(position) + orientation * collider.localOffset;
    auto hits = m_CollisionService->OverlapOBB(center, collider.halfExtents, orientation, 1u);
    if (!hits.empty())
    {
      static bool s_LoggedBlock = false;
      if (!s_LoggedBlock)
      {
        YA_LOG_INFO("Physics", "Car blocked by static collider, entity=%u",
          static_cast<uint32_t>(hits[0]));
        s_LoggedBlock = true;
      }
      position = prevPosition;
    }
  }

  GetScene().GetTransform(car).position = position;
  GetScene().GetTransform(car).rotation = rotation;
  GetScene().MarkDirty(car);

  if (m_Camera != entt::null && GetScene().HasComponent<FollowCameraComponent>(m_Camera))
  {
    auto& follow = GetScene().GetComponent<FollowCameraComponent>(m_Camera);
    auto& cam = GetScene().GetComponent<YAEngine::CameraComponent>(m_Camera);
    auto& camTc = GetScene().GetTransform(m_Camera);

    glm::dvec3 eulerDegrees = glm::dvec3(160.0, -0.0, -180.0);
    glm::dvec3 eulerRadians = glm::radians(eulerDegrees);
    glm::dquat extraRot = glm::dquat(eulerRadians);

    glm::dquat camRot = camTc.rotation;
    glm::dquat targetRot = rotation * extraRot;

    double t = 1.0 - std::exp(-follow.smoothSpeed * dt);

    camRot = glm::slerp(camRot, targetRot, t);
    camTc.rotation = camRot;

    double normSpeed = glm::clamp(vehicle->speed / vehicle->maxSpeed, 0.0, 1.0);
    double backFactor = glm::smoothstep(0.5, 1.0, normSpeed);

    glm::dvec3 dynamicOffset = follow.offset;
    dynamicOffset.z *= glm::mix(1.0, 0.6, backFactor);

    glm::dvec3 targetPos = position + rotation * dynamicOffset;

    glm::dvec3 camPos = camTc.position;
    camPos = glm::mix(camPos, targetPos, t);
    camTc.position = camPos;

    if (vehicle->speed > .01)
    {
      double speedNorm = glm::clamp(vehicle->speed / vehicle->maxSpeed, 0.0, 1.0);
      double factor = glm::smoothstep(0.0, 1.0, speedNorm);
      double targetFov = glm::mix(follow.baseFov, follow.maxFov, factor);

      double lerpSpeed = 5.0;
      cam.fov = float(glm::mix(double(cam.fov), targetFov, 1.0 - std::exp(-lerpSpeed * dt)));
    }
  }

  auto wheelView = GetScene().GetView<WheelComponent, YAEngine::LocalTransform>();

  for (auto wheelEntity : wheelView)
  {
    auto& wc = GetScene().GetComponent<WheelComponent>(wheelEntity);
    auto& tc = GetScene().GetTransform(wheelEntity);

    wc.spinAngle += (vehicle->speed / wc.radius) * dt;

    glm::quat steerRot = glm::identity<glm::quat>();
    if (wc.isFront)
    {
      steerRot = glm::angleAxis(vehicle->wheelsSteer * 10.0f, glm::dvec3(0,1,0));
    }

    glm::quat spinRot = glm::angleAxis(wc.spinAngle, glm::dvec3(1,0,0));

    tc.rotation = steerRot * glm::quat(wc.baseRot) * spinRot;
  }
}
