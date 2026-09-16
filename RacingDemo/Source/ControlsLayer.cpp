#include "ControlsLayer.h"
#include "Input/InputSystem.h"
#include "Render/Render.h"
#include "Assets/AssetManager.h"
#include "Utils/Log.h"

namespace
{
  // Body lean along a motion path, per m/s^2 of acceleration, and its limits. A sports car on
  // stiff springs: a hard corner leans a few degrees at most.
  const double kRollPerAccel = glm::radians(0.55);
  const double kMaxRoll = glm::radians(3.5);
  const double kPitchPerAccel = glm::radians(0.45);
  const double kMaxPitch = glm::radians(2.0);

  // Rotation of a world matrix with the scale divided out; model nodes are often scaled
  glm::quat WorldRotation(const glm::mat4& world)
  {
    glm::mat3 basis(world);
    for (int i = 0; i < 3; i++)
    {
      float length = glm::length(basis[i]);
      if (length > 1e-6f)
        basis[i] /= length;
    }
    return glm::normalize(glm::quat_cast(basis));
  }
}

void ControlsLayer::Update(double dt)
{
  if (m_Car == entt::null) return;
  if (!GetScene().HasComponent<VehicleComponent>(m_Car)) return;

  if (m_TerrainSystem == nullptr)
    m_TerrainSystem = &m_Registry->Get<YAEngine::TerrainSystem>();
  if (m_CollisionService == nullptr)
    m_CollisionService = &m_Registry->Get<YAEngine::CollisionQueryService>();
  if (!m_SparkTexture)
    m_SparkTexture = GetAssets().Textures().Load(
      GetAssets().ResolvePath("Assets/Textures/spark.png"));
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

  if (input.IsKeyPressed(YAEngine::Key::C))
    b_StaticLookAt = !b_StaticLookAt;

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

  if (!b_AxleGeometryResolved)
  {
    glm::dquat currentYawRot = glm::angleAxis(vehicle->yaw, glm::dvec3(0, 1, 0));
    ResolveAxleGeometry(currentYawRot * glm::dvec3(0, 0, 1), position);
  }

  // While a timeline session runs, a car with a motion path follows it instead of the input
  if (DriveAlongPath(dt))
    return;

  bool arrowLeft  = m_InputOverride.active ? m_InputOverride.left  : input.IsKeyDown(YAEngine::Key::Left);
  bool arrowRight = m_InputOverride.active ? m_InputOverride.right : input.IsKeyDown(YAEngine::Key::Right);
  bool arrowUp    = m_InputOverride.active ? m_InputOverride.up    : input.IsKeyDown(YAEngine::Key::Up);
  bool arrowDown  = m_InputOverride.active ? m_InputOverride.down  : input.IsKeyDown(YAEngine::Key::Down);

  if (arrowLeft)
    vehicle->wheelsSteer = glm::min(vehicle->wheelsSteer + vehicle->steerRate * dt, vehicle->maxSteerAngle);
  if (arrowRight)
    vehicle->wheelsSteer = glm::max(vehicle->wheelsSteer - vehicle->steerRate * dt, -vehicle->maxSteerAngle);
  if (!arrowLeft && !arrowRight)
  {
    double recenter = vehicle->steerReturnRate * dt;
    if (vehicle->wheelsSteer > 0.0) vehicle->wheelsSteer = glm::max(vehicle->wheelsSteer - recenter, 0.0);
    else if (vehicle->wheelsSteer < 0.0) vehicle->wheelsSteer = glm::min(vehicle->wheelsSteer + recenter, 0.0);
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

  // Kinematic bicycle model: yaw rate comes from geometry, not a tuned curve, and vanishes with speed to avoid pivot-in-place slide at a crawl.
  double yawRate = vehicle->speed * std::tan(vehicle->wheelsSteer) / m_WheelBase;

  double prevYaw = vehicle->yaw;
  vehicle->yaw += yawRate * dt;
  glm::dquat yawRot = glm::angleAxis(vehicle->yaw, glm::dvec3(0, 1, 0));
  glm::dquat prevYawRot = glm::angleAxis(prevYaw, glm::dvec3(0, 1, 0));

  glm::dvec3 forward = yawRot * glm::dvec3(0, 0, 1);
  glm::dvec3 prevForward = prevYawRot * glm::dvec3(0, 0, 1);
  // Midpoint heading keeps the traced arc accurate when a frame covers a large yaw step
  glm::dvec3 midForward = glm::angleAxis(prevYaw + yawRate * dt * 0.5, glm::dvec3(0, 1, 0))
    * glm::dvec3(0, 0, 1);

  glm::dvec3 prevPosition = position;
  glm::dvec3 rearAxle = prevPosition + prevForward * m_RearAxleOffset
    + midForward * vehicle->speed * dt;
  glm::dvec3 deltaXZ = rearAxle - forward * m_RearAxleOffset - prevPosition;
  deltaXZ.y = 0.0;

  auto snapY = [this](glm::dvec3 p) -> glm::dvec3 { return SnapToGround(p); };

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

  // Invariant guard: revert yaw if it rotated the OBB into a wall, else next frame's wasOverlapping check would let the car tunnel through the collider.
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

    glm::dvec3 contactNormal(0.0, 0.0, 1.0);
    if (blockerEntity != entt::null)
      contactNormal = contactNormalXZ(blockerEntity, prevPosition);
    glm::vec3 emitOrigin = glm::vec3(prevPosition) + glm::vec3(forward) * 0.9f + glm::vec3(0.0f, 0.4f, 0.0f);
    glm::vec3 emitNormal = glm::vec3(contactNormal.x, 0.5f, contactNormal.z);
    double speedFactor = glm::clamp(std::abs(vehicle->speed) / vehicle->maxSpeed, 0.0, 1.0);
    uint32_t burstCount = 80u + static_cast<uint32_t>(speedFactor * 160.0);
    m_SparkPool.Emit(emitOrigin, emitNormal, burstCount);
  }
  vehicle->wasInContact = inContact;

  glm::dquat targetTilt = TerrainTilt(position);

  double tiltSmooth = 1.0 - std::exp(-10.0 * dt);
  vehicle->tilt = glm::slerp(vehicle->tilt, targetTilt, tiltSmooth);

  glm::dquat rotation = vehicle->tilt * yawRot;

  // The terrain lift keeps the body clear of the sampled heightfield; a fixed plane needs no slack
  GetScene().GetTransform(car).position = position + glm::dvec3(0, b_FixedGround ? 0.0 : 0.05, 0);
  GetScene().GetTransform(car).rotation = rotation;
  GetScene().MarkDirty(car);

  UpdateFollowCamera(dt, position, yawRot, *vehicle);
  UpdateWheels(dt, *vehicle, 0.0, false);
  UpdateSparks(dt);
}

void ControlsLayer::ResolveAxleGeometry(const glm::dvec3& forwardXZ, const glm::dvec3& carPos)
{
  glm::dvec3 frontSum(0.0);
  glm::dvec3 rearSum(0.0);
  int frontCount = 0;
  int rearCount = 0;

  auto wheelView = GetScene().GetView<WheelComponent, YAEngine::WorldTransform>();
  for (auto wheelEntity : wheelView)
  {
    auto& wc = GetScene().GetComponent<WheelComponent>(wheelEntity);
    auto& wt = GetScene().GetComponent<YAEngine::WorldTransform>(wheelEntity);
    glm::dvec3 wheelPos = glm::dvec3(glm::vec3(wt.world[3]));

    if (wc.isFront) { frontSum += wheelPos; frontCount++; }
    else            { rearSum  += wheelPos; rearCount++; }
  }

  if (frontCount == 0 || rearCount == 0) return;

  glm::dvec3 frontMid = frontSum / static_cast<double>(frontCount);
  glm::dvec3 rearMid = rearSum / static_cast<double>(rearCount);

  double wheelBase = glm::dot(frontMid - rearMid, forwardXZ);
  // World transforms are still identity until the hierarchy is first evaluated
  if (wheelBase < 0.1) return;

  m_WheelBase = wheelBase;
  m_RearAxleOffset = glm::dot(rearMid - carPos, forwardXZ);
  b_AxleGeometryResolved = true;

  YA_LOG_INFO("Physics", "Car axle geometry resolved: wheelBase=%.3f rearAxleOffset=%.3f",
    m_WheelBase, m_RearAxleOffset);
}

bool ControlsLayer::DriveAlongPath(double dt)
{
  auto& scene = GetScene();
  auto& player = m_Registry->Get<YAEngine::SequencePlayer>();

  auto* path = scene.GetRegistry().try_get<YAEngine::MotionPathComponent>(m_Car);
  // The player leaves the car to this layer, which also has the wheels and the body to move
  if (path != nullptr)
    path->externallyDriven = true;

  if (path == nullptr || path->points.size() < 2 || !player.IsActive())
  {
    if (b_PathDriven)
      ReleasePathDrive();
    return false;
  }

  b_PathDriven = true;

  auto& vehicle = scene.GetComponent<VehicleComponent>(m_Car);
  const YAEngine::MotionPathPose pose = YAEngine::EvaluateMotionPath(
    path->GetTable(), path->speedKeys, float(player.GetTime()));

  glm::dvec3 forward(pose.forward);
  glm::dquat yawRot = glm::angleAxis(double(pose.yaw), glm::dvec3(0, 1, 0));
  // The curve is the rear axle's: that is the point the kinematic model rolls without slip
  glm::dvec3 position = SnapToGround(glm::dvec3(pose.position) - forward * m_RearAxleOffset);

  // Same bicycle model as the input drive, solved for the steering the curve asks for
  vehicle.yaw = double(pose.yaw);
  vehicle.yawInitialized = true;
  vehicle.speed = double(pose.speed);
  vehicle.wheelsSteer = glm::clamp(std::atan(m_WheelBase * double(pose.curvature)),
    -vehicle.maxSteerAngle, vehicle.maxSteerAngle);
  vehicle.tilt = TerrainTilt(position);
  vehicle.wasInContact = false;

  auto& transform = scene.GetTransform(m_Car);
  transform.position = position + glm::dvec3(0, b_FixedGround ? 0.0 : 0.05, 0);
  transform.rotation = vehicle.tilt * yawRot;
  scene.MarkDirty(m_Car);

  CaptureBody(player);
  ApplyBodyLean(pose);

  UpdateFollowCamera(dt, position, yawRot, vehicle);
  UpdateWheels(dt, vehicle, double(pose.distance), true);
  UpdateSparks(dt);
  return true;
}

void ControlsLayer::ReleasePathDrive()
{
  b_PathDriven = false;
  b_BodyCaptured = false;
  m_BodyParts.clear();

  auto& scene = GetScene();
  // The session has put every transform back; the input model restarts from them
  for (const auto& wheel : m_WheelSpins)
  {
    if (scene.GetRegistry().valid(wheel.entity) && scene.HasComponent<WheelComponent>(wheel.entity))
      scene.GetComponent<WheelComponent>(wheel.entity).spinAngle = wheel.spinAngle;
  }
  m_WheelSpins.clear();

  if (m_Car == entt::null || !scene.HasComponent<VehicleComponent>(m_Car))
    return;

  auto& vehicle = scene.GetComponent<VehicleComponent>(m_Car);
  // The input drive runs later in this same frame and writes its heading back, so it has to
  // start from the restored one right away
  glm::dvec3 forward = glm::dquat(scene.GetTransform(m_Car).rotation) * glm::dvec3(0, 0, 1);
  vehicle.yaw = std::atan2(forward.x, forward.z);
  vehicle.yawInitialized = true;
  vehicle.speed = 0.0;
  vehicle.wheelsSteer = 0.0;
  vehicle.tilt = glm::dquat(1, 0, 0, 0);
}

void ControlsLayer::CaptureBody(YAEngine::SequencePlayer& player)
{
  auto& scene = GetScene();

  if (!b_BodyCaptured)
  {
    b_BodyCaptured = true;
    m_BodyParts.clear();
    m_WheelSpins.clear();

    YAEngine::Entity bodyParent = entt::null;
    bool sharedParent = true;
    glm::vec3 pivotSum(0.0f);
    for (auto [wheel, wc] : scene.GetView<WheelComponent>().each())
    {
      m_WheelSpins.push_back({ .entity = wheel, .spinAngle = wc.spinAngle });
      YAEngine::Entity parent = scene.GetHierarchy(wheel).parent;
      if (bodyParent == entt::null)
        bodyParent = parent;
      else if (parent != bodyParent)
        sharedParent = false;
      pivotSum += scene.GetTransform(wheel).position;
    }

    // Leaning the whole car would lift the wheels off the road; the body is every sibling of
    // the wheels, leaned around the axles
    if (!m_WheelSpins.empty() && sharedParent && bodyParent != entt::null
      && scene.HasComponent<YAEngine::WorldTransform>(bodyParent)
      && scene.HasComponent<YAEngine::WorldTransform>(m_Car))
    {
      m_BodyPivot = pivotSum / float(m_WheelSpins.size());
      // Fixed inside the model, so matrices from the previous frame give the same answer
      glm::quat carRotation = WorldRotation(scene.GetComponent<YAEngine::WorldTransform>(m_Car).world);
      glm::quat parentRotation = WorldRotation(scene.GetComponent<YAEngine::WorldTransform>(bodyParent).world);
      m_BodyFrame = glm::normalize(glm::inverse(carRotation) * parentRotation);

      for (YAEngine::Entity child = scene.GetHierarchy(bodyParent).firstChild; child != entt::null;
        child = scene.GetHierarchy(child).nextSibling)
      {
        // Brake callipers sit on the hubs next to the wheels ("mycar-wheelbrake...") and would
        // swing into the rims if they leaned with the body
        const std::string& name = scene.GetName(child);
        if (!scene.HasComponent<WheelComponent>(child) && name.find("wheel") == std::string::npos)
          m_BodyParts.push_back({ .entity = child, .base = scene.GetTransform(child) });
      }
    }
    else if (!m_WheelSpins.empty())
    {
      YA_LOG_WARN("Physics", "Car wheels do not share one parent, the body will not lean");
    }
  }

  // Every frame: a session restarted by Play or F9 begins with an empty restore list
  for (const auto& wheel : m_WheelSpins)
    player.ProtectTransform(scene, wheel.entity);
  for (const auto& part : m_BodyParts)
    player.ProtectTransform(scene, part.entity);
}

void ControlsLayer::ApplyBodyLean(const YAEngine::MotionPathPose& pose)
{
  if (m_BodyParts.empty())
    return;

  auto& scene = GetScene();

  // In the car's own frame +X is the side the car turns toward while its yaw grows and +Z is
  // forward. The body leans away from the turn centre and dives under braking: its up axis
  // tips toward this horizontal vector, by the vector's length.
  double lateral = double(pose.speed) * double(pose.speed) * double(pose.curvature);
  double roll = glm::clamp(lateral * kRollPerAccel, -kMaxRoll, kMaxRoll);
  double pitch = glm::clamp(-double(pose.acceleration) * kPitchPerAccel, -kMaxPitch, kMaxPitch);
  glm::dvec3 lean(-roll, 0.0, pitch);

  glm::quat carLean(1.0f, 0.0f, 0.0f, 0.0f);
  double angle = glm::length(lean);
  if (angle > 1e-6)
  {
    glm::dvec3 axis = glm::normalize(glm::cross(glm::dvec3(0.0, 1.0, 0.0), lean / angle));
    carLean = glm::quat(glm::angleAxis(angle, axis));
  }

  // The same rotation in the space the body parts live in
  glm::quat localLean = glm::normalize(glm::inverse(m_BodyFrame) * carLean * m_BodyFrame);

  for (const auto& part : m_BodyParts)
  {
    if (!scene.GetRegistry().valid(part.entity))
      continue;
    auto& transform = scene.GetTransform(part.entity);
    transform.position = m_BodyPivot + localLean * (part.base.position - m_BodyPivot);
    transform.rotation = glm::normalize(localLean * part.base.rotation);
    transform.scale = part.base.scale;
    scene.MarkDirty(part.entity);
  }
}

glm::dvec3 ControlsLayer::SnapToGround(glm::dvec3 p) const
{
  if (m_TerrainSystem != nullptr && m_TerrainEntity != entt::null
    && m_TerrainSystem->HasCachedHeight(static_cast<uint32_t>(m_TerrainEntity)))
  {
    p.y = static_cast<double>(m_TerrainSystem->SampleCachedHeight(
      static_cast<uint32_t>(m_TerrainEntity),
      static_cast<float>(p.x), static_cast<float>(p.z)));
  }
  else if (b_FixedGround)
  {
    p.y = m_GroundY;
  }
  return p;
}

glm::dquat ControlsLayer::TerrainTilt(const glm::dvec3& position) const
{
  glm::dquat targetTilt { 1, 0, 0, 0 };
  if (m_TerrainSystem != nullptr && m_TerrainEntity != entt::null
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

  return targetTilt;
}

void ControlsLayer::UpdateFollowCamera(double dt, const glm::dvec3& position, const glm::dquat& yawRot,
  const VehicleComponent& vehicle)
{
  if (m_Camera != entt::null && GetScene().HasComponent<FollowCameraComponent>(m_Camera))
  {
    auto& follow = GetScene().GetComponent<FollowCameraComponent>(m_Camera);
    auto& cam = GetScene().GetComponent<YAEngine::CameraComponent>(m_Camera);
    auto& camTc = GetScene().GetTransform(m_Camera);

    double t = 1.0 - std::exp(-follow.smoothSpeed * dt);

    if (b_StaticLookAt)
    {
      glm::dvec3 camPos = glm::dvec3(camTc.position);
      glm::dvec3 toCar = position - camPos;
      double len = glm::length(toCar);
      if (len > 1e-6)
      {
        glm::dvec3 dir = toCar / len;
        double pitch = std::asin(glm::clamp(dir.y, -1.0, 1.0));
        double yaw = std::atan2(-dir.x, -dir.z);
        glm::dquat qYaw = glm::angleAxis(yaw, glm::dvec3(0, 1, 0));
        glm::dquat qPitch = glm::angleAxis(pitch, glm::dvec3(1, 0, 0));
        glm::dquat targetRot = qYaw * qPitch;
        glm::dquat camRot = glm::dquat(camTc.rotation);
        camTc.rotation = glm::slerp(camRot, targetRot, t);

        const double nearDist = 15.0;
        const double farDist = 100.0;
        const double minFov = glm::radians(15.0);
        double zoomT = glm::smoothstep(nearDist, farDist, len);
        double targetFov = glm::mix(follow.baseFov, minFov, zoomT);
        double fovLerp = 1.0 - std::exp(-5.0 * dt);
        cam.fov = float(glm::mix(double(cam.fov), targetFov, fovLerp));
      }
    }
    else
    {
      glm::dvec3 eulerDegrees = glm::dvec3(160.0, -0.0, -180.0);
      glm::dvec3 eulerRadians = glm::radians(eulerDegrees);
      glm::dquat extraRot = glm::dquat(eulerRadians);

      glm::dquat camRot = camTc.rotation;
      glm::dquat targetRot = yawRot * extraRot;

      camRot = glm::slerp(camRot, targetRot, t);
      camTc.rotation = camRot;

      double normSpeed = 0;//glm::clamp(vehicle.speed / vehicle.maxSpeed, 0.0, 1.0);
      double backFactor = glm::smoothstep(0.5, 1.0, normSpeed);

      glm::dvec3 dynamicOffset = follow.offset;
      dynamicOffset.z *= glm::mix(1.0, 0.6, backFactor);

      glm::dvec3 targetPos = position + yawRot * dynamicOffset;

      glm::dvec3 camPos = camTc.position;
      camPos = glm::mix(camPos, targetPos, t);
      camTc.position = camPos;

      if (vehicle.speed > .01)
      {
        double speedNorm = glm::clamp(vehicle.speed / vehicle.maxSpeed, 0.0, 1.0);
        double factor = glm::smoothstep(0.0, 1.0, speedNorm);
        double targetFov = glm::mix(follow.baseFov, follow.maxFov, factor);

        double lerpSpeed = 5.0;
        cam.fov = float(glm::mix(double(cam.fov), targetFov, 1.0 - std::exp(-lerpSpeed * dt)));
      }
    }
  }
}

void ControlsLayer::UpdateWheels(double dt, const VehicleComponent& vehicle, double pathDistance, bool fromPath)
{
  auto wheelView = GetScene().GetView<WheelComponent, YAEngine::LocalTransform>();

  for (auto wheelEntity : wheelView)
  {
    auto& wc = GetScene().GetComponent<WheelComponent>(wheelEntity);
    auto& tc = GetScene().GetTransform(wheelEntity);

    // Along a path the spin is a function of the distance, so scrubbing back turns the wheels back
    if (fromPath)
      wc.spinAngle = pathDistance / wc.radius;
    else
      wc.spinAngle += (vehicle.speed / wc.radius) * dt;

    glm::quat steerRot = glm::identity<glm::quat>();
    if (wc.isFront)
    {
      steerRot = glm::angleAxis(vehicle.wheelsSteer, glm::dvec3(0,0,1));
    }

    glm::quat spinRot = glm::angleAxis(wc.spinAngle, glm::dvec3(1,0,0));

    tc.rotation = steerRot * glm::quat(wc.baseRot) * spinRot;
  }
}

void ControlsLayer::UpdateSparks(double dt)
{
  m_SparkPool.Update(dt);
  if (m_SparkPool.HasAliveSparks() && m_SparkTexture)
  {
    m_SparkInstances.clear();
    m_SparkPool.FillInstances(m_SparkInstances);
    if (!m_SparkInstances.empty())
      GetRender().SubmitParticles(m_SparkInstances, m_SparkTexture);
  }
}
