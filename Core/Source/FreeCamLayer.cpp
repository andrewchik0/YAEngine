#include "FreeCamLayer.h"

#include "Application.h"

namespace YAEngine
{
  void FreeCamLayer::Init()
  {
    App().GetScene().AddComponent<CameraComponent>(freeCam);
    App().GetScene().GetTransform(freeCam).position = glm::vec3(0.0f, 0.0f, -1.0f);
    App().GetScene().SetActiveCamera(freeCam);

    glm::quat qPitch = glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::quat qYaw   = glm::angleAxis(yaw,   glm::vec3(0.0f, 1.0f, 0.0f));

    glm::quat orientation = qYaw * qPitch;
    App().GetScene().GetTransform(freeCam).rotation = glm::normalize(orientation);
  }

  void FreeCamLayer::Update(double deltaTime)
  {
    if (mousePressed)
    {
      yaw   -= (float)deltaX * (float)deltaTime * 3.0f;
      pitch -= (float)deltaY * (float)deltaTime * 3.0f;

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
      velocity = glm::normalize(velocity) * (float)deltaTime * 2.0f;
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
}