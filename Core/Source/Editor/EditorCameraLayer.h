#pragma once

#include "Events.h"
#include "EventBus.h"
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
    void OnDetach() override;

  private:

    Entity m_Camera {};

    float m_Yaw = 0.0f;
    float m_Pitch = 0.0f;

    bool b_KeyA = false;
    bool b_KeyD = false;
    bool b_KeyW = false;
    bool b_KeyS = false;

    bool b_MousePressed = false;

    double m_PrevX = 0.0, m_PrevY = 0.0, m_DeltaX = 0.0, m_DeltaY = 0.0;

    float m_Speed = 1.0f;

    SubscriptionId m_OnKeyboard {}, m_OnMouseMove {}, m_OnMouseButton {}, m_OnMouseScroll {};

    void ResetInputState();

    void OnKeyboard(const KeyEvent& event);
    void OnMouseMoved(const MouseMovedEvent& event);
    void OnMouseButton(const MouseButtonEvent& event);
    void OnMouseScroll(const MouseWheelEvent& event);
  };
}
