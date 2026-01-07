#include "FreeCamLayer.h"

#include <iostream>

#include "Application.h"

namespace YAEngine
{
  void FreeCamLayer::Init()
  {
    freeCam = App().GetScene().CreateEntity("FreeCam");
    App().GetScene().AddComponent<CameraComponent>(freeCam);
    App().GetScene().GetTransform(freeCam).position = glm::vec3(0.0f, 0.0f, 3.0f);
    App().GetScene().SetActiveCamera(freeCam);

    glm::quat qPitch = glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::quat qYaw   = glm::angleAxis(yaw,   glm::vec3(0.0f, 1.0f, 0.0f));

    glm::quat orientation = qYaw * qPitch;
    App().GetScene().GetTransform(freeCam).rotation = glm::normalize(orientation);

    onMouseButton = App().Events().Subscribe<MouseButtonEvent>([&](const MouseButtonEvent& e) { OnMouseButton(e); });
    onMouseMove = App().Events().Subscribe<MouseMovedEvent>([&](const MouseMovedEvent& e) { OnMouseMoved(e); });
    onKeyboard = App().Events().Subscribe<KeyEvent>([&](const KeyEvent& e) { OnKeyboard(e); });
    onMouseScroll = App().Events().Subscribe<MouseWheelEvent>([&](const MouseWheelEvent& e) { OnMouseScroll(e); });
  }

  void FreeCamLayer::Destroy()
  {
    App().Events().Unsubscribe<MouseButtonEvent>(onMouseButton);
    App().Events().Unsubscribe<MouseMovedEvent>(onMouseMove);
    App().Events().Unsubscribe<KeyEvent>(onKeyboard);
    App().Events().Unsubscribe<MouseButtonEvent>(onMouseScroll);
  }


  void FreeCamLayer::Update(double deltaTime)
  {
    if (mousePressed)
    {
      yaw   -= float(deltaX * deltaTime) * 2.0f;
      pitch -= float(deltaY * deltaTime) * 2.0f;

      deltaX = 0.0f;
      deltaY = 0.0f;

      float maxPitch = glm::radians(90.0f);
      pitch = glm::clamp(pitch, -maxPitch, maxPitch);

      glm::quat qPitch = glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));
      glm::quat qYaw   = glm::angleAxis(yaw,   glm::vec3(0.0f, 1.0f, 0.0f));

      glm::quat orientation = qYaw * qPitch;
      App().GetScene().GetTransform(freeCam).rotation = glm::normalize(orientation);
    }

    glm::vec3 forward = App().GetScene().GetTransform(freeCam).rotation * glm::vec3(0, 0, -1);
    glm::vec3 right   = App().GetScene().GetTransform(freeCam).rotation * glm::vec3(1, 0, 0);
    glm::vec3 up      = App().GetScene().GetTransform(freeCam).rotation * glm::vec3(0, 1, 0);

    glm::vec3 velocity(0.0f);
    velocity += forward * float(keyWPressed - keySPressed);
    velocity += right * float(keyDPressed - keyAPressed);

    if (velocity != glm::vec3(0.0f))
    {
      velocity = glm::normalize(velocity) * (float)deltaTime * 2.0f * speed;
      App().GetScene().GetTransform(freeCam).position += velocity;
    }
  }

  void FreeCamLayer::OnKeyboard(const YAEngine::KeyEvent& event)
  {
    if (event.key == GLFW_KEY_A)
    {
      keyAPressed = event.action == GLFW_PRESS ? true : event.action == GLFW_RELEASE ? false : keyAPressed;
    }
    if (event.key == GLFW_KEY_D)
    {
      keyDPressed = event.action == GLFW_PRESS ? true : event.action == GLFW_RELEASE ? false : keyDPressed;
    }

    if (event.key == GLFW_KEY_W)
    {
      keyWPressed = event.action == GLFW_PRESS ? true : event.action == GLFW_RELEASE ? false : keyWPressed;
    }
    if (event.key == GLFW_KEY_S)
    {
      keySPressed = event.action == GLFW_PRESS ? true : event.action == GLFW_RELEASE ? false : keySPressed;
    }
  }

  void FreeCamLayer::OnMouseMoved(const YAEngine::MouseMovedEvent& event)
  {
    deltaX = event.x - prevX;
    deltaY = event.y - prevY;

    prevX = event.x;
    prevY = event.y;
  }

  void FreeCamLayer::OnMouseButton(const YAEngine::MouseButtonEvent& event)
  {
    mousePressed = event.action == GLFW_PRESS;
  }

  void FreeCamLayer::OnMouseScroll(const MouseWheelEvent& event)
  {
    if (event.y > 0)
    {
      speed *= 1.1f;
    }
    else if (event.y < 0)
    {
      speed *= 0.9f;
    }
    speed = glm::clamp(speed, 0.01f, 1000.0f);
  }

}