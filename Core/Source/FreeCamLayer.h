#pragma once

#include "Pch.h"
#include "Layer.h"
#include "Scene/Scene.h"

namespace YAEngine
{
  class FreeCamLayer : public Layer
  {
  public:
    FreeCamLayer() = default;

    Entity freeCam;

    float yaw = 0;
    float pitch = 0.0f;

    bool keyAPressed = false;
    bool keyDPressed = false;
    bool keyWPressed = false;
    bool keySPressed = false;

    bool mousePressed = false;

    double prevX = 0.0, prevY = 0.0, deltaX = 0.0, deltaY = 0.0;

    void Init() override;
    void Update(double deltaTime) override;

    void OnKeyboard(const YAEngine::KeyEvent& event) override;
    void OnMouseMoved(const YAEngine::MouseMovedEvent& event) override;
    void OnMouseButton(const YAEngine::MouseButtonEvent& event) override;

  };
}
