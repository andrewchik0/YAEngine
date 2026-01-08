#include "ControlsLayer.h"
#include "AppLayer.h"

void ControlsLayer::Update(double dt)
{
  auto car = App().GetLayer<AppLayer>()->car;

  glm::dvec3 position = App().GetScene().GetTransform(car).position;
  glm::dquat rotation = App().GetScene().GetTransform(car).rotation;

  if (arrowLeft)
  {
    wheelsSteer = glm::clamp(wheelsSteer + 0.1 * dt, 0.0, 0.03);
  }
  if (arrowRight)
  {
    wheelsSteer = glm::clamp(wheelsSteer - 0.1 * dt, -0.03, 0.0);
  }
  if (!arrowLeft && !arrowRight)
  {
    if (wheelsSteer > 0.0) wheelsSteer = glm::clamp(wheelsSteer - 0.2 * dt, 0.0, 0.03);
    if (wheelsSteer < 0.0) wheelsSteer = glm::clamp(wheelsSteer + 0.2 * dt, -0.03, 0.0);
  }

  if (arrowUp)
    speed += acceleration * dt;
  else if (arrowDown)
  {
    if (speed > 0)
      speed -= brake * dt;
    else
      speed -= accelerationBack * dt;
  }
  else
    speed -= drag * dt * (speed > 0 ? 1 : speed < 0 ? -1 : 0);

  speed = glm::clamp(speed, -maxSpeedBack, maxSpeed);

  double turnSpeed = glm::radians(260.0 - speed / maxSpeed * 160.0);
  double turn = wheelsSteer * (1.0 / 0.03) * turnSpeed * dt * (speed / maxSpeed);

  glm::dquat rotDelta = glm::angleAxis(turn, glm::dvec3(0,1,0));
  rotation = rotDelta * rotation;

  glm::dvec3 forward = rotation * glm::dvec3(0, 0, 1);
  position += forward * speed * dt;

  App().GetScene().GetTransform(car).position = position;
  App().GetScene().GetTransform(car).rotation = rotation;
  App().GetScene().MarkDirty(car);

  glm::dvec3 eulerDegrees = glm::dvec3(160.0, -0.0, -180.0);
  glm::dvec3 eulerRadians = glm::radians(eulerDegrees);
  glm::dquat extraRot = glm::dquat(eulerRadians);

  glm::dquat camRot = App().GetScene().GetTransform(camera).rotation;
  glm::dquat targetRot = rotation * extraRot;

  double camSmooth = 8.0;
  double t = 1.0 - std::exp(-camSmooth * dt);

  camRot = glm::slerp(camRot, targetRot, t);
  App().GetScene().GetTransform(camera).rotation = camRot;

  double normSpeed = glm::clamp(speed / maxSpeed, 0.0, 1.0);
  double backFactor = glm::smoothstep(0.5, 1.0, normSpeed);

  glm::dvec3 dynamicOffset = cameraOffset;
  dynamicOffset.z *= glm::mix(1.0, 0.6, backFactor);

  glm::dvec3 targetPos = position + rotation * dynamicOffset;

  glm::dvec3 camPos = App().GetScene().GetTransform(camera).position;
  camPos = glm::mix(camPos, targetPos, t);
  App().GetScene().GetTransform(camera).position = camPos;

  if (speed > .01)
  {
    auto& cam = App().GetScene().GetComponent<YAEngine::CameraComponent>(camera);
    double minFov = glm::radians(58.31);
    double maxFov = glm::radians(75.0);
    double normSpeed = glm::clamp(speed / maxSpeed, 0.0, 1.0);
    double factor = glm::smoothstep(0.0, 1.0, normSpeed);
    double targetFov = glm::mix(minFov, maxFov, factor);

    double lerpSpeed = 5.0;
    cam.fov = float(glm::mix(double(cam.fov), targetFov, 1.0 - std::exp(-lerpSpeed * dt)));
  }
}