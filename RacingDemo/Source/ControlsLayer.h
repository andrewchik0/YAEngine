#pragma once
#include "Application.h"
#include "GameComponents.h"

class ControlsLayer : public YAEngine::Layer
{
public:
  YAEngine::SubscriptionId m_OnKeyboard;

  bool b_ArrowLeft = false;
  bool b_ArrowRight = false;
  bool b_ArrowUp = false;
  bool b_ArrowDown = false;

  YAEngine::Entity m_Car = entt::null;
  YAEngine::Entity m_Camera = entt::null;

  void OnSceneReady() override
  {
    m_OnKeyboard = App().Events().Subscribe<YAEngine::KeyEvent>([&](auto event) { OnKeyboard(event); });

    m_Camera = App().GetScene().CreateEntity("camera");
    App().GetScene().AddComponent<YAEngine::CameraComponent>(m_Camera);
    App().GetScene().AddComponent<FollowCameraComponent>(m_Camera);

    glm::dvec3 eulerDegrees = glm::vec3(160.0, -0.0, -180.0);
    glm::dvec3 eulerRadians = glm::radians(eulerDegrees);
    App().GetScene().GetTransform(m_Camera).rotation = glm::quat(eulerRadians);
  }

  void SetTarget(YAEngine::Entity car)
  {
    m_Car = car;
    if (m_Camera != entt::null)
      App().GetScene().GetComponent<FollowCameraComponent>(m_Camera).target = car;
  }

  void OnDetach() override
  {
    App().Events().Unsubscribe<YAEngine::KeyEvent>(m_OnKeyboard);
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

    if (e.key == GLFW_KEY_LEFT) b_ArrowLeft = value;
    if (e.key == GLFW_KEY_RIGHT) b_ArrowRight = value;
    if (e.key == GLFW_KEY_UP) b_ArrowUp = value;
    if (e.key == GLFW_KEY_DOWN) b_ArrowDown = value;
  }
};
