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
#ifdef YA_EDITOR
    // A captured cursor is nowhere near the viewport as far as ImGui is concerned, but
    // the drag that captured it started there and has to keep receiving input.
    bool IsViewportHovered() const { return b_ViewportHovered || b_MouseCaptured; }

    // Look mode: hides and locks the cursor, then puts it back where it was grabbed.
    void SetMouseCaptured(bool captured);
    bool IsMouseCaptured() const { return b_MouseCaptured; }
#else
    bool IsViewportHovered() const { return b_ViewportHovered; }
#endif

    void SetGizmoDragging(bool dragging) { b_GizmoDragging = dragging; }
    bool IsGizmoDragging() const { return b_GizmoDragging; }

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
    bool b_GizmoDragging = false;
#ifdef YA_EDITOR
    bool b_MouseCaptured = false;
    double m_CaptureRestoreX = 0.0, m_CaptureRestoreY = 0.0;
#endif
  };
}
