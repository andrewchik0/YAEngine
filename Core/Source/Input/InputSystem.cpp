#include "Input/InputSystem.h"
#include <imgui.h>

namespace YAEngine
{
  void InputSystem::ProcessEvents()
  {
    const auto& windowEventStack = m_Window.PollEvents();

    ImGuiIO* io = b_ImGuiFiltering ? &ImGui::GetIO() : nullptr;
    // IsViewportHovered, not the raw flag - ImGui keyboard nav would filter WASD during a captured look otherwise.
    bool passThrough = IsViewportHovered();

    for (const auto& windowEvent : windowEventStack)
    {
      if (auto* ke = std::get_if<KeyEvent>(&windowEvent))
      {
        int32_t key = ke->key;
        if (key >= 0 && key < MAX_KEYS)
        {
          bool filtered = io && io->WantCaptureKeyboard && !passThrough;
          if (ke->action == GLFW_PRESS && !filtered)
            m_KeyState[key] = true;
          else if (ke->action == GLFW_RELEASE)
            m_KeyState[key] = false;
        }
      }
      else if (auto* me = std::get_if<MouseButtonEvent>(&windowEvent))
      {
        int32_t btn = me->button;
        if (btn >= 0 && btn < MAX_MOUSE_BUTTONS)
        {
          bool filtered = io && io->WantCaptureMouse && !passThrough;
          if (me->action == GLFW_PRESS && !filtered)
            m_MouseState[btn] = true;
          else if (me->action == GLFW_RELEASE)
            m_MouseState[btn] = false;
        }
      }
      else if (auto* me = std::get_if<MouseMovedEvent>(&windowEvent))
      {
        if (b_FirstMouseMove)
        {
          m_MouseX = me->x;
          m_MouseY = me->y;
          b_FirstMouseMove = false;
        }
        else
        {
          m_MouseDeltaX += me->x - m_MouseX;
          m_MouseDeltaY += me->y - m_MouseY;
          m_MouseX = me->x;
          m_MouseY = me->y;
        }
      }
      else if (auto* se = std::get_if<MouseWheelEvent>(&windowEvent))
      {
        bool filtered = io && io->WantCaptureMouse && !passThrough;
        if (!filtered)
        {
          m_ScrollDeltaX += se->x;
          m_ScrollDeltaY += se->y;
        }
      }
    }
  }

#ifdef YA_EDITOR
  void InputSystem::SetMouseCaptured(bool captured)
  {
    if (captured == b_MouseCaptured)
      return;

    b_MouseCaptured = captured;
    GLFWwindow* window = m_Window.Get();

    if (captured)
    {
      glfwGetCursorPos(window, &m_CaptureRestoreX, &m_CaptureRestoreY);
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
      // Skips the pointer acceleration curve the OS applies to a visible cursor, which
      // is what makes a captured look feel 1:1 with the hand instead of sluggish
      if (glfwRawMouseMotionSupported())
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    else
    {
      if (glfwRawMouseMotionSupported())
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
      glfwSetCursorPos(window, m_CaptureRestoreX, m_CaptureRestoreY);
    }

    // Both transitions teleport the reported position. Without this the jump arrives as
    // one enormous delta and throws the camera across the scene.
    b_FirstMouseMove = true;
  }
#endif

  void InputSystem::EndFrame()
  {
    m_PrevKeyState = m_KeyState;
    m_PrevMouseState = m_MouseState;
    m_MouseDeltaX = 0.0;
    m_MouseDeltaY = 0.0;
    m_ScrollDeltaX = 0.0;
    m_ScrollDeltaY = 0.0;
  }

  bool InputSystem::IsKeyDown(Key key) const
  {
    int32_t k = static_cast<int32_t>(key);
    if (k < 0 || k >= MAX_KEYS) return false;
    return m_KeyState[k];
  }

  bool InputSystem::IsKeyPressed(Key key) const
  {
    int32_t k = static_cast<int32_t>(key);
    if (k < 0 || k >= MAX_KEYS) return false;
    return m_KeyState[k] && !m_PrevKeyState[k];
  }

  bool InputSystem::IsKeyReleased(Key key) const
  {
    int32_t k = static_cast<int32_t>(key);
    if (k < 0 || k >= MAX_KEYS) return false;
    return !m_KeyState[k] && m_PrevKeyState[k];
  }

  bool InputSystem::IsMouseDown(MouseButton btn) const
  {
    int32_t b = static_cast<int32_t>(btn);
    if (b < 0 || b >= MAX_MOUSE_BUTTONS) return false;
    return m_MouseState[b];
  }

  bool InputSystem::IsMousePressed(MouseButton btn) const
  {
    int32_t b = static_cast<int32_t>(btn);
    if (b < 0 || b >= MAX_MOUSE_BUTTONS) return false;
    return m_MouseState[b] && !m_PrevMouseState[b];
  }

  bool InputSystem::IsMouseReleased(MouseButton btn) const
  {
    int32_t b = static_cast<int32_t>(btn);
    if (b < 0 || b >= MAX_MOUSE_BUTTONS) return false;
    return !m_MouseState[b] && m_PrevMouseState[b];
  }

  glm::vec2 InputSystem::GetMousePosition() const
  {
    return { float(m_MouseX), float(m_MouseY) };
  }

  glm::vec2 InputSystem::GetMouseDelta() const
  {
    return { float(m_MouseDeltaX), float(m_MouseDeltaY) };
  }

  glm::vec2 InputSystem::GetScrollDelta() const
  {
    return { float(m_ScrollDeltaX), float(m_ScrollDeltaY) };
  }
}
