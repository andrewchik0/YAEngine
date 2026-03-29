#include "Editor/EditorCameraLayer.h"

#include "Input/InputSystem.h"
#include "Scene/Components.h"

namespace YAEngine
{
  void EditorCameraLayer::OnSceneReady()
  {
    m_Camera = GetScene().CreateEntity("EditorCamera");
    GetScene().AddComponent<CameraComponent>(m_Camera);
    GetScene().AddComponent<EditorOnlyTag>(m_Camera);
    m_Pitch = initialPitch;
    m_Yaw = initialYaw;
    GetScene().GetTransform(m_Camera).position = initialPosition;
    GetScene().SetActiveCamera(m_Camera);

    glm::quat qPitch = glm::angleAxis(m_Pitch, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::quat qYaw   = glm::angleAxis(m_Yaw,   glm::vec3(0.0f, 1.0f, 0.0f));

    glm::quat orientation = qYaw * qPitch;
    GetScene().GetTransform(m_Camera).rotation = glm::normalize(orientation);
  }

  void EditorCameraLayer::Update(double deltaTime)
  {
    if (GetScene().GetActiveCamera() != m_Camera) return;

    auto& input = GetInput();

    if (!input.IsViewportHovered())
      return;

    if (input.IsMouseDown(MouseButton::Right))
    {
      auto delta = input.GetMouseDelta();

      m_Yaw   -= delta.x * .0015f;
      m_Pitch -= delta.y * .0015f;

      float maxPitch = glm::radians(90.0f);
      m_Pitch = glm::clamp(m_Pitch, -maxPitch, maxPitch);

      glm::quat qPitch = glm::angleAxis(m_Pitch, glm::vec3(1.0f, 0.0f, 0.0f));
      glm::quat qYaw   = glm::angleAxis(m_Yaw,   glm::vec3(0.0f, 1.0f, 0.0f));

      glm::quat orientation = qYaw * qPitch;
      GetScene().GetTransform(m_Camera).rotation = glm::normalize(orientation);
    }

    auto& rot = GetScene().GetTransform(m_Camera).rotation;
    glm::vec3 forward = rot * glm::vec3(0, 0, -1);
    glm::vec3 right   = rot * glm::vec3(1, 0, 0);
    glm::vec3 up      = rot * glm::vec3(0, 1, 0);

    if (input.IsMouseDown(MouseButton::Middle))
    {
      auto delta = input.GetMouseDelta();
      float panSpeed = 0.003f;
      GetScene().GetTransform(m_Camera).position -= right * delta.x * panSpeed;
      GetScene().GetTransform(m_Camera).position += up * delta.y * panSpeed;
    }

    glm::vec3 velocity(0.0f);
    velocity += forward * float(input.IsKeyDown(Key::W) - input.IsKeyDown(Key::S));
    velocity += right * float(input.IsKeyDown(Key::D) - input.IsKeyDown(Key::A));

    if (velocity != glm::vec3(0.0f))
    {
      velocity = glm::normalize(velocity) * (float)deltaTime * 2.0f * m_Speed;
      GetScene().GetTransform(m_Camera).position += velocity;
    }

    float scrollY = input.GetScrollDelta().y;
    if (scrollY != 0.0f)
    {
      if (scrollY > 0)
        m_Speed *= 1.1f;
      else
        m_Speed *= 0.9f;
      m_Speed = glm::clamp(m_Speed, 0.01f, 1000.0f);
    }
  }
}
