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

  private:

    void ApplyRotation();

    Entity m_Camera = entt::null;

    float m_Yaw = 0.0f;
    float m_Pitch = 0.0f;
    float m_Speed = 1.0f;
  };
}
