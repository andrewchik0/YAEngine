#include "InputSystem.h"
#include <imgui.h>

namespace YAEngine
{
  void InputSystem::ProcessEvents()
  {
    const auto& windowEventStack = m_Window.PollEvents();

    ImGuiIO* io = m_ImGuiFiltering ? &ImGui::GetIO() : nullptr;

    for (auto& windowEvent : windowEventStack)
    {
      switch (windowEvent->type)
      {
      case EventType::Key:
        if (!io || !io->WantCaptureKeyboard)
          m_EventBus.Emit<KeyEvent>(*dynamic_cast<KeyEvent*>(windowEvent.get()));
        break;
      case EventType::MouseButton:
        if (!io || !io->WantCaptureMouse)
          m_EventBus.Emit<MouseButtonEvent>(*dynamic_cast<MouseButtonEvent*>(windowEvent.get()));
        break;
      case EventType::MouseScroll:
        if (!io || !io->WantCaptureMouse)
          m_EventBus.Emit<MouseWheelEvent>(*dynamic_cast<MouseWheelEvent*>(windowEvent.get()));
        break;
      case EventType::MouseMoved:
        m_EventBus.Emit<MouseMovedEvent>(*dynamic_cast<MouseMovedEvent*>(windowEvent.get()));
        break;
      case EventType::Resize:
        m_EventBus.Emit<ResizeEvent>(*dynamic_cast<ResizeEvent*>(windowEvent.get()));
        break;
      default: break;
      }
    }
  }
}
