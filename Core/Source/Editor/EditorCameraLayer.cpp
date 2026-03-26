#include "Editor/EditorCameraLayer.h"

#include "Application.h"
#include "Scene/Components.h"

namespace YAEngine
{
  void EditorCameraLayer::OnSceneReady()
  {
    m_Camera = App().GetScene().CreateEntity("EditorCamera");
    App().GetScene().AddComponent<CameraComponent>(m_Camera);
    App().GetScene().AddComponent<EditorOnlyTag>(m_Camera);
    App().GetScene().GetTransform(m_Camera).position = glm::vec3(0.0f, 0.0f, 3.0f);
    App().GetScene().SetActiveCamera(m_Camera);

    glm::quat qPitch = glm::angleAxis(m_Pitch, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::quat qYaw   = glm::angleAxis(m_Yaw,   glm::vec3(0.0f, 1.0f, 0.0f));

    glm::quat orientation = qYaw * qPitch;
    App().GetScene().GetTransform(m_Camera).rotation = glm::normalize(orientation);

    m_OnMouseButton = App().Events().Subscribe<MouseButtonEvent>([&](const MouseButtonEvent& e) { OnMouseButton(e); });
    m_OnMouseMove = App().Events().Subscribe<MouseMovedEvent>([&](const MouseMovedEvent& e) { OnMouseMoved(e); });
    m_OnKeyboard = App().Events().Subscribe<KeyEvent>([&](const KeyEvent& e) { OnKeyboard(e); });
    m_OnMouseScroll = App().Events().Subscribe<MouseWheelEvent>([&](const MouseWheelEvent& e) { OnMouseScroll(e); });
  }

  void EditorCameraLayer::OnDetach()
  {
    App().Events().Unsubscribe<MouseButtonEvent>(m_OnMouseButton);
    App().Events().Unsubscribe<MouseMovedEvent>(m_OnMouseMove);
    App().Events().Unsubscribe<KeyEvent>(m_OnKeyboard);
    App().Events().Unsubscribe<MouseWheelEvent>(m_OnMouseScroll);
  }

  void EditorCameraLayer::ResetInputState()
  {
    b_KeyA = false;
    b_KeyD = false;
    b_KeyW = false;
    b_KeyS = false;
    b_MousePressed = false;
    m_DeltaX = 0.0;
    m_DeltaY = 0.0;
  }

  void EditorCameraLayer::Update(double deltaTime)
  {
    if (App().GetScene().GetActiveCamera() != m_Camera) return;

    if (!App().GetInputSystem().IsViewportHovered())
    {
      ResetInputState();
      return;
    }

    if (b_MousePressed)
    {
      m_Yaw   -= float(m_DeltaX) * .0015f;
      m_Pitch -= float(m_DeltaY) * .0015f;

      m_DeltaX = 0.0;
      m_DeltaY = 0.0;

      float maxPitch = glm::radians(90.0f);
      m_Pitch = glm::clamp(m_Pitch, -maxPitch, maxPitch);

      glm::quat qPitch = glm::angleAxis(m_Pitch, glm::vec3(1.0f, 0.0f, 0.0f));
      glm::quat qYaw   = glm::angleAxis(m_Yaw,   glm::vec3(0.0f, 1.0f, 0.0f));

      glm::quat orientation = qYaw * qPitch;
      App().GetScene().GetTransform(m_Camera).rotation = glm::normalize(orientation);
    }

    glm::vec3 forward = App().GetScene().GetTransform(m_Camera).rotation * glm::vec3(0, 0, -1);
    glm::vec3 right   = App().GetScene().GetTransform(m_Camera).rotation * glm::vec3(1, 0, 0);

    glm::vec3 velocity(0.0f);
    velocity += forward * float(b_KeyW - b_KeyS);
    velocity += right * float(b_KeyD - b_KeyA);

    if (velocity != glm::vec3(0.0f))
    {
      velocity = glm::normalize(velocity) * (float)deltaTime * 2.0f * m_Speed;
      App().GetScene().GetTransform(m_Camera).position += velocity;
    }
  }

  void EditorCameraLayer::OnKeyboard(const KeyEvent& event)
  {
    if (event.key == GLFW_KEY_A)
      b_KeyA = event.action == GLFW_PRESS ? true : event.action == GLFW_RELEASE ? false : b_KeyA;
    if (event.key == GLFW_KEY_D)
      b_KeyD = event.action == GLFW_PRESS ? true : event.action == GLFW_RELEASE ? false : b_KeyD;
    if (event.key == GLFW_KEY_W)
      b_KeyW = event.action == GLFW_PRESS ? true : event.action == GLFW_RELEASE ? false : b_KeyW;
    if (event.key == GLFW_KEY_S)
      b_KeyS = event.action == GLFW_PRESS ? true : event.action == GLFW_RELEASE ? false : b_KeyS;
  }

  void EditorCameraLayer::OnMouseMoved(const MouseMovedEvent& event)
  {
    if (b_FirstMouseMove)
    {
      m_PrevX = event.x;
      m_PrevY = event.y;
      b_FirstMouseMove = false;
      return;
    }

    m_DeltaX = event.x - m_PrevX;
    m_DeltaY = event.y - m_PrevY;

    m_PrevX = event.x;
    m_PrevY = event.y;
  }

  void EditorCameraLayer::OnMouseButton(const MouseButtonEvent& event)
  {
    if (event.button != GLFW_MOUSE_BUTTON_RIGHT) return;
    b_MousePressed = event.action == GLFW_PRESS;
  }

  void EditorCameraLayer::OnMouseScroll(const MouseWheelEvent& event)
  {
    if (event.y > 0)
      m_Speed *= 1.1f;
    else if (event.y < 0)
      m_Speed *= 0.9f;
    m_Speed = glm::clamp(m_Speed, 0.01f, 1000.0f);
  }
}
