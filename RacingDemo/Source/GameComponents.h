#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entt.hpp>

struct VehicleComponent
{
  double speed = 0.0;
  double wheelsSteer = 0.0;
  double maxSpeed = 30.0;
  double maxSpeedBack = 5.0;
  double acceleration = 10.0;
  double accelerationBack = 5.0;
  double brake = 15.0;
  double drag = 5.0;
};

struct WheelComponent
{
  glm::dquat baseRot { 1, 0, 0, 0 };
  double spinAngle = 0.0;
  bool isFront = false;
  float radius = 0.29f;
};

struct FollowCameraComponent
{
  entt::entity target { entt::null };
  glm::dvec3 offset { 0.0, 1.9, -3.6 };
  double baseFov = glm::radians(58.31);
  double maxFov = glm::radians(75.0);
  double smoothSpeed = 8.0;
};
