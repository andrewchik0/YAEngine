#pragma once

#include "Window.h"
#include "EventBus.h"

namespace YAEngine
{
  class InputSystem
  {
  public:
    InputSystem(Window& window, EventBus& eventBus)
      : m_Window(window), m_EventBus(eventBus) {}

    void ProcessEvents();

    void SetImGuiFiltering(bool enabled) { m_ImGuiFiltering = enabled; }

  private:
    Window& m_Window;
    EventBus& m_EventBus;
    bool m_ImGuiFiltering = false;
  };
}
