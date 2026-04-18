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

  if (!vehicle->yawInitialized)
  {
    glm::dquat initRot = GetScene().GetTransform(car).rotation;
    glm::dvec3 initFwd = initRot * glm::dvec3(0, 0, 1);
    vehicle->yaw = std::atan2(initFwd.x, initFwd.z);
    vehicle->yawInitialized = true;
  }

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

  double prevYaw = vehicle->yaw;
  vehicle->yaw += turn;
  glm::dquat yawRot = glm::angleAxis(vehicle->yaw, glm::dvec3(0, 1, 0));
  glm::dquat prevYawRot = glm::angleAxis(prevYaw, glm::dvec3(0, 1, 0));

  glm::dvec3 forward = yawRot * glm::dvec3(0, 0, 1);
  glm::dvec3 prevPosition = position;
  glm::dvec3 deltaXZ = forward * vehicle->speed * dt;
  deltaXZ.y = 0.0;

  bool hasTerrainCache = (m_TerrainEntity != entt::null
    && m_TerrainSystem->HasCachedHeight(static_cast<uint32_t>(m_TerrainEntity)));

  auto snapY = [&](glm::dvec3 p) -> glm::dvec3
  {
    if (hasTerrainCache)
      p.y = static_cast<double>(m_TerrainSystem->SampleCachedHeight(
        static_cast<uint32_t>(m_TerrainEntity),
        static_cast<float>(p.x), static_cast<float>(p.z)));
    return p;
  };

  bool hasCollider = GetScene().HasComponent<YAEngine::ColliderComponent>(car);

  auto obbHit = [&](const glm::dvec3& tryPos, const glm::dquat& tryYaw, entt::entity* outHit) -> bool
  {
    if (!hasCollider) return false;
    auto& collider = GetScene().GetComponent<YAEngine::ColliderComponent>(car);
    glm::quat orientation = glm::quat(tryYaw);
    glm::vec3 center = glm::vec3(tryPos) + orientation * collider.localOffset;
    auto hits = m_CollisionService->OverlapOBB(center, collider.halfExtents, orientation, 1u);
    if (hits.empty()) return false;
    if (outHit) *outHit = hits[0];
    return true;
  };

  // Derives contact normal (XZ unit vector pointing away from wall) by picking the dominant
  // separating axis between the car and the hit collider's AABB. Works for both single and instanced.
  auto contactNormalXZ = [&](entt::entity hit, const glm::dvec3& carPos) -> glm::dvec3
  {
    glm::vec3 bCenter(0.0f);
    glm::vec3 bHalf(1.0f);

    if (GetScene().HasComponent<YAEngine::ColliderComponent>(hit)
      && GetScene().HasComponent<YAEngine::WorldTransform>(hit))
    {
      auto& c = GetScene().GetComponent<YAEngine::ColliderComponent>(hit);
      auto& wt = GetScene().GetComponent<YAEngine::WorldTransform>(hit);
      bCenter = glm::vec3(wt.world * glm::vec4(c.localOffset, 1.0f));
      glm::vec3 scale(
        glm::length(glm::vec3(wt.world[0])),
        glm::length(glm::vec3(wt.world[1])),
        glm::length(glm::vec3(wt.world[2])));
      bHalf = c.halfExtents * scale;
    }
    else if (GetScene().HasComponent<YAEngine::InstancedColliderComponent>(hit))
    {
      auto& ic = GetScene().GetComponent<YAEngine::InstancedColliderComponent>(hit);
      double bestDist2 = std::numeric_limits<double>::infinity();
      for (auto& e : ic.instances)
      {
        double dx = static_cast<double>(e.center.x) - carPos.x;
        double dz = static_cast<double>(e.center.z) - carPos.z;
        double d2 = dx * dx + dz * dz;
        if (d2 < bestDist2)
        {
          bestDist2 = d2;
          bCenter = e.center;
          bHalf = e.halfExtents;
        }
      }
    }

    double dx = carPos.x - static_cast<double>(bCenter.x);
    double dz = carPos.z - static_cast<double>(bCenter.z);
    double nx = dx / std::max(1e-6, static_cast<double>(bHalf.x));
    double nz = dz / std::max(1e-6, static_cast<double>(bHalf.z));

    if (std::abs(nx) >= std::abs(nz))
      return glm::dvec3(nx >= 0.0 ? 1.0 : -1.0, 0.0, 0.0);
    return glm::dvec3(0.0, 0.0, nz >= 0.0 ? 1.0 : -1.0);
  };

  // If we were already overlapping at the frame start, allow any motion so the car can escape.
  bool wasOverlapping = hasCollider && obbHit(snapY(prevPosition), prevYawRot, nullptr);

  glm::dvec3 finalXZ = prevPosition + deltaXZ;
  bool translationFullyBlocked = false;
  entt::entity blockerEntity = entt::null;

  if (!wasOverlapping && hasCollider)
  {
    entt::entity hitId = entt::null;
    if (obbHit(snapY(finalXZ), yawRot, &hitId))
    {
      blockerEntity = hitId;
      glm::dvec3 N = contactNormalXZ(hitId, prevPosition);

      double deltaLen = glm::length(deltaXZ);
      double normComp = glm::dot(deltaXZ, N);
      // Kill only the into-wall component; preserve away-from-wall motion.
      glm::dvec3 tComp = deltaXZ - std::min(0.0, normComp) * N;
      double tLen = glm::length(tComp);

      // Head-on hit (nearly all motion was into the wall): full stop.
      const double kHeadOnThreshold = 0.1;
      if (deltaLen < 1e-6 || tLen < kHeadOnThreshold * deltaLen)
      {
        finalXZ = prevPosition;
        translationFullyBlocked = true;
      }
      else
      {
        // Scraping friction: lose a fraction of tangent motion per second.
        const double kSlideFriction = 2.5;
        double slideKeep = std::max(0.0, 1.0 - kSlideFriction * dt);
        glm::dvec3 slideDelta = tComp * slideKeep;
        glm::dvec3 slidePos = prevPosition + slideDelta;

        if (obbHit(snapY(slidePos), yawRot, nullptr))
        {
          finalXZ = prevPosition;
          translationFullyBlocked = true;
        }
        else
        {
          finalXZ = slidePos;
          vehicle->speed *= slideKeep;

          // Alignment torque: rotate yaw toward the slide direction so the body scrapes
          // along the wall instead of staying pointed into it.
          double slideDeltaLen = glm::length(slideDelta);
          if (slideDeltaLen > 1e-6)
          {
            glm::dvec3 slideDir = slideDelta / slideDeltaLen;
            double targetYaw = std::atan2(slideDir.x, slideDir.z);
            double yawDiff = targetYaw - vehicle->yaw;
            const double kPi = 3.141592653589793;
            while (yawDiff > kPi) yawDiff -= 2.0 * kPi;
            while (yawDiff < -kPi) yawDiff += 2.0 * kPi;
            const double kAlignRate = glm::radians(150.0);
            double yawStep = glm::clamp(yawDiff, -kAlignRate * dt, kAlignRate * dt);
            double alignedYaw = vehicle->yaw + yawStep;
            glm::dquat alignedRot = glm::angleAxis(alignedYaw, glm::dvec3(0, 1, 0));
            if (!obbHit(snapY(finalXZ), alignedRot, nullptr))
            {
              vehicle->yaw = alignedYaw;
              yawRot = alignedRot;
            }
          }
        }
      }
    }
  }

  position = snapY(finalXZ);

  if (translationFullyBlocked)
    vehicle->speed = 0.0;

  // Invariant guard: end-of-frame state must not overlap. If a player-induced turn rotated
  // the OBB into a wall at a full-stop position, revert yaw to the known-clean previous value.
  // Without this guard, the next frame's wasOverlapping would disable enforcement, letting
  // the car tunnel through the collider.
  if (hasCollider && !wasOverlapping && obbHit(position, yawRot, nullptr))
  {
    vehicle->yaw = prevYaw;
    yawRot = prevYawRot;
  }

  bool inContact = translationFullyBlocked;
  if (inContact && !vehicle->wasInContact)
  {
    YA_LOG_INFO("Physics", "Car contact begin, collider entity=%u",
      static_cast<uint32_t>(blockerEntity));
  }
  vehicle->wasInContact = inContact;

  glm::dquat targetTilt { 1, 0, 0, 0 };
  if (m_TerrainEntity != entt::null
    && m_TerrainSystem->HasCachedHeight(static_cast<uint32_t>(m_TerrainEntity)))
  {
    uint32_t tid = static_cast<uint32_t>(m_TerrainEntity);
    float sx = static_cast<float>(position.x);
    float sz = static_cast<float>(position.z);
    float step = 1.0f;
    float hL = m_TerrainSystem->SampleCachedHeight(tid, sx - step, sz);
    float hR = m_TerrainSystem->SampleCachedHeight(tid, sx + step, sz);
    float hD = m_TerrainSystem->SampleCachedHeight(tid, sx, sz - step);
    float hU = m_TerrainSystem->SampleCachedHeight(tid, sx, sz + step);
    glm::dvec3 normal = glm::normalize(glm::dvec3(
      static_cast<double>(hL - hR) / (2.0 * static_cast<double>(step)),
      1.0,
      static_cast<double>(hD - hU) / (2.0 * static_cast<double>(step))));
    glm::dvec3 up(0.0, 1.0, 0.0);
    glm::dvec3 axis = glm::cross(up, normal);
    double axisLen = glm::length(axis);
    if (axisLen > 1e-6)
    {
      double cosA = glm::clamp(glm::dot(up, normal), -1.0, 1.0);
      targetTilt = glm::angleAxis(std::acos(cosA), axis / axisLen);
    }
  }

  double tiltSmooth = 1.0 - std::exp(-10.0 * dt);
  vehicle->tilt = glm::slerp(vehicle->tilt, targetTilt, tiltSmooth);

  glm::dquat rotation = vehicle->tilt * yawRot;

  GetScene().GetTransform(car).position = position + glm::dvec3(0,0.05,0);
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
    glm::dquat targetRot = yawRot * extraRot;

    double t = 1.0 - std::exp(-follow.smoothSpeed * dt);

    camRot = glm::slerp(camRot, targetRot, t);
    camTc.rotation = camRot;

    double normSpeed = glm::clamp(vehicle->speed / vehicle->maxSpeed, 0.0, 1.0);
    double backFactor = glm::smoothstep(0.5, 1.0, normSpeed);

    glm::dvec3 dynamicOffset = follow.offset;
    dynamicOffset.z *= glm::mix(1.0, 0.6, backFactor);

    glm::dvec3 targetPos = position + yawRot * dynamicOffset;

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
      steerRot = glm::angleAxis(vehicle->wheelsSteer * 10.0f, glm::dvec3(0,0,1));
    }

    glm::quat spinRot = glm::angleAxis(wc.spinAngle, glm::dvec3(1,0,0));

    tc.rotation = steerRot * glm::quat(wc.baseRot) * spinRot;
  }
}
