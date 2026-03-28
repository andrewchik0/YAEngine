#pragma once
#include "Layer.h"
#include "Events.h"
#include "EventBus.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
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
    m_OnKeyboard = Events().Subscribe<YAEngine::KeyEvent>([&](auto event) { OnKeyboard(event); });

    m_Camera = GetScene().CreateEntity("camera");
    GetScene().AddComponent<YAEngine::CameraComponent>(m_Camera);
    GetScene().AddComponent<FollowCameraComponent>(m_Camera);

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

  void OnDetach() override
  {
    Events().Unsubscribe<YAEngine::KeyEvent>(m_OnKeyboard);
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
