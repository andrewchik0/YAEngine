#pragma once
#include "Application.h"

class AppLayer;

class ControlsLayer : public YAEngine::Layer
{
public:
  YAEngine::Entity camera;

  YAEngine::SubscriptionId onKeyboard;

  bool arrowLeft = false;
  bool arrowRight = false;
  bool arrowUp = false;
  bool arrowDown = false;

  double wheelsSteer = 0.0;
  glm::dvec3 velocity = glm::dvec3(0.0f);
  double speed = 0.0f;
  double maxSpeed = 30.0f;
  double maxSpeedBack = 5.0f;
  double acceleration = 10.0f;
  double accelerationBack = 5.0f;
  double brake = 15.0f;
  double drag = 5.0f;
  glm::dvec3 cameraOffset;

  void Init() override
  {
    App().Events().Subscribe<YAEngine::KeyEvent>([&](auto event) { OnKeyboard(event); });

    camera = App().GetScene().CreateEntity("camera");
    App().GetScene().AddComponent<YAEngine::CameraComponent>(camera);
    cameraOffset = glm::vec3(0.0, 1.7, -3.6);

    glm::dvec3 eulerDegrees = glm::vec3(160.0, -0.0, -180.0);
    glm::dvec3 eulerRadians = glm::radians(eulerDegrees);
    App().GetScene().GetTransform(camera).rotation = glm::quat(eulerRadians);
  }

  void Destroy() override
  {
    App().Events().Unsubscribe<YAEngine::KeyEvent>(onKeyboard);
  }

  void Update(double deltaTime) override;

  void OnKeyboard(YAEngine::KeyEvent& e)
  {
    bool value;
    if (e.action == GLFW_PRESS)
      value = true;
    else if (e.action == GLFW_RELEASE)
      value = false;
    else return;

    if (e.key == GLFW_KEY_LEFT) arrowLeft = value;
    if (e.key == GLFW_KEY_RIGHT) arrowRight = value;
    if (e.key == GLFW_KEY_UP) arrowUp = value;
    if (e.key == GLFW_KEY_DOWN) arrowDown = value;
  }
};

