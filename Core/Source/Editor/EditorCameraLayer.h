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

  private:

    Entity m_Camera {};

    float m_Yaw = 0.0f;
    float m_Pitch = 0.0f;
    float m_Speed = 1.0f;
  };
}
