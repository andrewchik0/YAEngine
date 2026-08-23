#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entt.hpp>

struct VehicleComponent
{
  double speed = 0.0;
  // Front wheel angle in radians, positive turns left
  double wheelsSteer = 0.0;
  double maxSteerAngle = glm::radians(20.0);
  double steerRate = glm::radians(60.0);
  double steerReturnRate = glm::radians(120.0);
  double maxSpeed = 10.0;
  double maxSpeedBack = 3.0;
  double acceleration = 5.0;
  double accelerationBack = 3.0;
  double brake = 10.0;
  double drag = 5.0;
  double yaw = 0.0;
  bool yawInitialized = false;
  bool wasInContact = false;
  glm::dquat tilt { 1, 0, 0, 0 };
};

struct WheelComponent
{
  glm::dquat baseRot { 1, 0, 0, 0 };
  double spinAngle = 0.0;
  bool isFront = false;
  float radius = 0.44f;
};

struct FollowCameraComponent
{
  entt::entity target { entt::null };
  glm::dvec3 offset { 0.0, 3.4, -6.6 };
  double baseFov = glm::radians(58.31);
  double maxFov = glm::radians(58.31);
  double smoothSpeed = 8.0;
};
