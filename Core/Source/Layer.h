#pragma once

#include "Events.h"

namespace YAEngine
{
  class Application;

  class Layer
  {
  public:
    virtual ~Layer() = default;

    Application& App();

    virtual void Init() {}
    virtual void Destroy() {}
    virtual void Update(double deltaTime) {}

    virtual void OnBeforeLoad() {}
    virtual void OnMouseMoved(const MouseMovedEvent& event) {}
    virtual void OnMouseButton(const MouseButtonEvent& event) {}
    virtual void OnKeyboard(const KeyEvent& event) {}
    virtual void OnResize(const ResizeEvent& event) {}

  private:
  };
}