#include "Editor/EditorCameraLayer.h"

#include "Input/InputSystem.h"
#include "Scene/Components.h"
#include "Utils/CameraOrientation.h"

namespace YAEngine
{
  namespace
  {
    float ClampPitch(float pitch)
    {
      float maxPitch = glm::radians(90.0f);
      return glm::clamp(pitch, -maxPitch, maxPitch);
    }
  }

  void EditorCameraLayer::OnSceneReady()
  {
    auto& camState = GetScene().GetEditorCameraState();
    m_Camera = GetScene().CreateEntity("EditorCamera");
    GetScene().AddComponent<CameraComponent>(m_Camera);
    GetScene().AddComponent<EditorOnlyTag>(m_Camera);
    m_Pitch = camState.pitch;
    m_Yaw = camState.yaw;
    b_InputLocked = false;
    b_Flown = false;
    GetScene().GetTransform(m_Camera).position = camState.position;
    GetScene().SetActiveCamera(m_Camera);

    ApplyRotation();
  }

  glm::vec3 EditorCameraLayer::GetPosition()
  {
    if (m_Camera != entt::null && GetScene().HasComponent<LocalTransform>(m_Camera))
      return GetScene().GetTransform(m_Camera).position;
    return GetScene().GetEditorCameraState().position;
  }

  void EditorCameraLayer::SetPose(const glm::vec3& position, float yaw, float pitch)
  {
    if (m_Camera == entt::null || !GetScene().HasComponent<LocalTransform>(m_Camera))
      return;

    m_Yaw = yaw;
    m_Pitch = ClampPitch(pitch);
    GetScene().GetTransform(m_Camera).position = position;
    ApplyRotation();
  }

  void EditorCameraLayer::SetPoseFromRotation(const glm::vec3& position, const glm::quat& rotation)
  {
    const glm::vec3 angles = YawPitchRollFromRotation(rotation);
    SetPose(position, angles.x, angles.y);
  }

  float EditorCameraLayer::GetFov()
  {
    if (m_Camera == entt::null || !GetScene().HasComponent<CameraComponent>(m_Camera))
      return CameraComponent {}.fov;
    return GetScene().GetComponent<CameraComponent>(m_Camera).fov;
  }

  void EditorCameraLayer::SetFov(float radians)
  {
    if (m_Camera == entt::null || !GetScene().HasComponent<CameraComponent>(m_Camera) || !std::isfinite(radians))
      return;
    GetScene().GetComponent<CameraComponent>(m_Camera).fov = radians;
  }

  bool EditorCameraLayer::ConsumeFlown()
  {
    bool flown = b_Flown;
    b_Flown = false;
    return flown;
  }

  void EditorCameraLayer::ApplyRotation()
  {
    GetScene().GetTransform(m_Camera).rotation = MakeYawPitchRotation(m_Yaw, m_Pitch);
  }

  void EditorCameraLayer::Update(double deltaTime)
  {
    auto& input = GetInput();

    // Checked before every early-out, else a release outside the viewport, after a camera
    // change, or lost to focus change would leave the cursor stuck captured.
    if (input.IsMouseCaptured() && !input.IsMouseDown(MouseButton::Right))
      input.SetMouseCaptured(false);

    if (GetScene().GetActiveCamera() != m_Camera) return;

    if (!input.IsViewportHovered() || input.IsGizmoDragging())
      return;

    float scrollY = input.GetScrollDelta().y;
    if (scrollY != 0.0f)
      SetSpeed(m_Speed * (scrollY > 0.0f ? 1.1f : 0.9f));

    if (!b_InputLocked)
      Fly(input, float(deltaTime));
  }

  void EditorCameraLayer::Fly(InputSystem& input, float deltaTime)
  {
    if (input.IsMouseDown(MouseButton::Right))
    {
      input.SetMouseCaptured(true);

      auto delta = input.GetMouseDelta();

      m_Yaw   -= delta.x * .0015f;
      m_Pitch -= delta.y * .0015f;
      m_Pitch = ClampPitch(m_Pitch);
      b_Flown |= delta != glm::vec2(0.0f);

      ApplyRotation();
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
      b_Flown |= delta != glm::vec2(0.0f);
    }

    glm::vec3 velocity(0.0f);
    velocity += forward * float(input.IsKeyDown(Key::W) - input.IsKeyDown(Key::S));
    velocity += right * float(input.IsKeyDown(Key::D) - input.IsKeyDown(Key::A));

    if (velocity != glm::vec3(0.0f))
    {
      velocity = glm::normalize(velocity) * deltaTime * m_Speed;
      GetScene().GetTransform(m_Camera).position += velocity;
      b_Flown = true;
    }
  }

  void EditorCameraLayer::SetSpeed(float metersPerSecond)
  {
    if (!std::isfinite(metersPerSecond))
      return;

    m_Speed = glm::clamp(metersPerSecond, MIN_SPEED, MAX_SPEED);
  }
}
