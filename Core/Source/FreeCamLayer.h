#pragma once

#include "Events.h"
#include "EventBus.h"
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

    float speed = 1.0;

    void Init() override;
    void Update(double deltaTime) override;
    void Destroy() override;

    SubscriptionId onKeyboard, onMouseMove, onMouseButton, onMouseScroll;
    void OnKeyboard(const KeyEvent& event);
    void OnMouseMoved(const MouseMovedEvent& event);
    void OnMouseButton(const MouseButtonEvent& event);
    void OnMouseScroll(const MouseWheelEvent& event);

  };
}
