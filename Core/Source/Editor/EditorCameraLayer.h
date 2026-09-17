#pragma once

#include "Layer.h"
#include "Scene/Scene.h"

namespace YAEngine
{
  class InputSystem;

  class EditorCameraLayer : public Layer
  {
  public:
    EditorCameraLayer() = default;

    void OnSceneReady() override;
    void Update(double deltaTime) override;

    glm::vec3 GetPosition();
    float GetYaw() const { return m_Yaw; }
    float GetPitch() const { return m_Pitch; }
    Entity GetCameraEntity() const { return m_Camera; }

    // Places the camera as flying there would: the next mouse look continues from this yaw and
    // pitch. Radians; the pitch is clamped to straight up or down.
    void SetPose(const glm::vec3& position, float yaw, float pitch);
    // The same from a world rotation. Flying has no roll, so only its yaw and pitch are kept.
    void SetPoseFromRotation(const glm::vec3& position, const glm::quat& rotation);

    // Vertical field of view of the camera component, radians
    float GetFov();
    void SetFov(float radians);

    // Mouse look, pan and WASD are ignored while locked; the wheel still sets the speed
    void SetInputLocked(bool locked) { b_InputLocked = locked; }
    // Whether mouse look, pan or WASD moved the camera since the last call
    bool ConsumeFlown();

    static constexpr float DEFAULT_SPEED = 2.0f;
    static constexpr float MIN_SPEED = 0.02f;
    static constexpr float MAX_SPEED = 2000.0f;

    // Fly speed in meters per second; the mouse wheel over the viewport scales it
    float GetSpeed() const { return m_Speed; }
    // Clamped to MIN_SPEED..MAX_SPEED
    void SetSpeed(float metersPerSecond);

  private:

    void ApplyRotation();
    void Fly(InputSystem& input, float deltaTime);

    Entity m_Camera = entt::null;

    float m_Yaw = 0.0f;
    float m_Pitch = 0.0f;
    float m_Speed = DEFAULT_SPEED;
    bool b_InputLocked = false;
    bool b_Flown = false;
  };
}
