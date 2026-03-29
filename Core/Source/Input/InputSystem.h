#pragma once

#include "Window.h"
#include "Utils/KeyCodes.h"

namespace YAEngine
{
  class InputSystem
  {
  public:
    explicit InputSystem(Window& window)
      : m_Window(window) {}

    void ProcessEvents();
    void EndFrame();

    bool IsKeyDown(Key key) const;
    bool IsKeyPressed(Key key) const;
    bool IsKeyReleased(Key key) const;

    bool IsMouseDown(MouseButton btn) const;
    bool IsMousePressed(MouseButton btn) const;
    bool IsMouseReleased(MouseButton btn) const;

    glm::vec2 GetMousePosition() const;
    glm::vec2 GetMouseDelta() const;
    glm::vec2 GetScrollDelta() const;

    void SetImGuiFiltering(bool enabled) { b_ImGuiFiltering = enabled; }
    void SetViewportHovered(bool hovered) { b_ViewportHovered = hovered; }
    bool IsViewportHovered() const { return b_ViewportHovered; }

  private:
    static constexpr int MAX_KEYS = 349;
    static constexpr int MAX_MOUSE_BUTTONS = 8;

    Window& m_Window;

    std::array<bool, MAX_KEYS> m_KeyState {};
    std::array<bool, MAX_KEYS> m_PrevKeyState {};
    std::array<bool, MAX_MOUSE_BUTTONS> m_MouseState {};
    std::array<bool, MAX_MOUSE_BUTTONS> m_PrevMouseState {};

    double m_MouseX = 0.0, m_MouseY = 0.0;
    double m_MouseDeltaX = 0.0, m_MouseDeltaY = 0.0;
    double m_ScrollDeltaX = 0.0, m_ScrollDeltaY = 0.0;
    bool b_FirstMouseMove = true;

    bool b_ImGuiFiltering = false;
    bool b_ViewportHovered = false;
  };
}
