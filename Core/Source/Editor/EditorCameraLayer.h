#pragma once

#include "Layer.h"
#include "Scene/Scene.h"

namespace YAEngine
{
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

    static constexpr float DEFAULT_SPEED = 2.0f;
    static constexpr float MIN_SPEED = 0.02f;
    static constexpr float MAX_SPEED = 2000.0f;

    // Fly speed in meters per second; the mouse wheel over the viewport scales it
    float GetSpeed() const { return m_Speed; }
    // Clamped to MIN_SPEED..MAX_SPEED
    void SetSpeed(float metersPerSecond);

  private:

    void ApplyRotation();

    Entity m_Camera = entt::null;

    float m_Yaw = 0.0f;
    float m_Pitch = 0.0f;
    float m_Speed = DEFAULT_SPEED;
  };
}
