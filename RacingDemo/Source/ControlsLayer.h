#pragma once
#include "Application.h"
#include "GameComponents.h"

class ControlsLayer : public YAEngine::Layer
{
public:
  YAEngine::SubscriptionId onKeyboard;

  bool arrowLeft = false;
  bool arrowRight = false;
  bool arrowUp = false;
  bool arrowDown = false;

  void OnSceneReady() override
  {
    onKeyboard = App().Events().Subscribe<YAEngine::KeyEvent>([&](auto event) { OnKeyboard(event); });

    auto camera = App().GetScene().CreateEntity("camera");
    App().GetScene().AddComponent<YAEngine::CameraComponent>(camera);
    App().GetScene().AddComponent<FollowCameraComponent>(camera);

    glm::dvec3 eulerDegrees = glm::vec3(160.0, -0.0, -180.0);
    glm::dvec3 eulerRadians = glm::radians(eulerDegrees);
    App().GetScene().GetTransform(camera).rotation = glm::quat(eulerRadians);
  }

  void OnDetach() override
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
