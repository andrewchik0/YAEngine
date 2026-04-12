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

  private:

    Entity m_Camera = entt::null;

    float m_Yaw = 0.0f;
    float m_Pitch = 0.0f;
    float m_Speed = 1.0f;
  };
}
