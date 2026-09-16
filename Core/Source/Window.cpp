#include "Window.h"
#include "Utils/Log.h"

namespace YAEngine
{
  // The monitor holding the largest part of the window, primary if it overlaps none.
  static GLFWmonitor* FindWindowMonitor(GLFWwindow* window)
  {
    int32_t windowX, windowY, windowWidth, windowHeight;
    glfwGetWindowPos(window, &windowX, &windowY);
    glfwGetWindowSize(window, &windowWidth, &windowHeight);

    int32_t monitorCount = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
    GLFWmonitor* best = glfwGetPrimaryMonitor();
    int64_t bestArea = 0;

    for (int32_t i = 0; i < monitorCount; i++)
    {
      const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
      if (!mode)
        continue;

      int32_t monitorX, monitorY;
      glfwGetMonitorPos(monitors[i], &monitorX, &monitorY);

      int64_t overlapX = std::max(0, std::min(windowX + windowWidth, monitorX + mode->width) - std::max(windowX, monitorX));
      int64_t overlapY = std::max(0, std::min(windowY + windowHeight, monitorY + mode->height) - std::max(windowY, monitorY));
      if (overlapX * overlapY > bestArea)
      {
        bestArea = overlapX * overlapY;
        best = monitors[i];
      }
    }

    return best;
  }

  void Window::KeyCallback(GLFWwindow* window, int32_t key, int32_t scancode, int32_t action, int32_t mods)
  {
    auto* self = static_cast<Window*>(
      glfwGetWindowUserPointer(window)
    );

    if (self)
      self->m_WindowEventStack.push_back(KeyEvent{key, scancode, action, mods});
  }

  void Window::CursorPositionCallback(GLFWwindow* window, double xpos, double ypos)
  {
    auto* self = static_cast<Window*>(
      glfwGetWindowUserPointer(window)
    );

    if (self)
      self->m_WindowEventStack.push_back(MouseMovedEvent{xpos, ypos});
  }

  void Window::MouseButtonCallback(GLFWwindow* window, int32_t button, int32_t action, int32_t mods)
  {
    auto* self = static_cast<Window*>(
      glfwGetWindowUserPointer(window)
    );

    if (self)
      self->m_WindowEventStack.push_back(MouseButtonEvent{button, action, mods});
  }

  void Window::FramebufferSizeCallback(GLFWwindow* window, int32_t width, int32_t height)
  {
    auto* self = static_cast<Window*>(
      glfwGetWindowUserPointer(window)
    );

    if (self)
    {
      self->m_WindowWidth = width;
      self->m_WindowHeight = height;
      self->b_Resized = true;
    }
  }

  void Window::MouseScrollCallback(GLFWwindow* window, double xOffset, double yOffset)
  {
    auto* self = static_cast<Window*>(
      glfwGetWindowUserPointer(window)
    );

    if (self)
      self->m_WindowEventStack.push_back(MouseWheelEvent{xOffset, yOffset});
  }

  Window::Window(const WindowSpecs& specs)
  {
    m_WindowHeight = specs.height;
    m_WindowWidth = specs.width;

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_WindowHandle = glfwCreateWindow(m_WindowWidth, m_WindowHeight, specs.title.c_str(), nullptr, nullptr);
    if (!m_WindowHandle)
    {
      glfwTerminate();
      YA_LOG_ERROR("Window", "Failed to create GLFW window");
      throw std::runtime_error("Failed to create GLFW window");
    }
    glfwSetWindowUserPointer(m_WindowHandle, this);
    glfwSetKeyCallback(m_WindowHandle, KeyCallback);
    glfwSetCursorPosCallback(m_WindowHandle, CursorPositionCallback);
    glfwSetMouseButtonCallback(m_WindowHandle, MouseButtonCallback);
    glfwSetFramebufferSizeCallback(m_WindowHandle, FramebufferSizeCallback);
    glfwSetScrollCallback(m_WindowHandle, MouseScrollCallback);
  }

  void Window::Destroy()
  {
    glfwDestroyWindow(m_WindowHandle);
    glfwTerminate();
  }

  bool Window::IsOpen() const
  {
    return !glfwWindowShouldClose(m_WindowHandle);
  }

  void Window::Close()
  {
    glfwSetWindowShouldClose(m_WindowHandle, GLFW_TRUE);
  }

  void Window::SetBorderlessFullscreen(bool enabled)
  {
    if (enabled == b_BorderlessFullscreen)
      return;

    if (enabled)
    {
      GLFWmonitor* monitor = FindWindowMonitor(m_WindowHandle);
      const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
      if (!mode)
      {
        YA_LOG_WARN("Window", "No monitor to cover, staying windowed");
        return;
      }

      int32_t monitorX, monitorY;
      glfwGetMonitorPos(monitor, &monitorX, &monitorY);

      b_RestoreMaximized = glfwGetWindowAttrib(m_WindowHandle, GLFW_MAXIMIZED);
      int32_t frameLeft, frameTop, frameRight, frameBottom;
      glfwGetWindowFrameSize(m_WindowHandle, &frameLeft, &frameTop, &frameRight, &frameBottom);

      if (!b_RestoreMaximized)
      {
        glfwGetWindowPos(m_WindowHandle, &m_RestoreX, &m_RestoreY);
        glfwGetWindowSize(m_WindowHandle, &m_RestoreWidth, &m_RestoreHeight);
      }

      glfwSetWindowAttrib(m_WindowHandle, GLFW_DECORATED, GLFW_FALSE);

      // An undecorated window must not stay maximized: GLFW maximizes it to the work area, not the
      // whole monitor. Restoring it only after the frame is gone skips the OS restore animation,
      // but it then lands on the old outer rect, so the frame is taken back out of the placement.
      if (b_RestoreMaximized)
      {
        glfwRestoreWindow(m_WindowHandle);
        glfwGetWindowPos(m_WindowHandle, &m_RestoreX, &m_RestoreY);
        glfwGetWindowSize(m_WindowHandle, &m_RestoreWidth, &m_RestoreHeight);
        m_RestoreX += frameLeft;
        m_RestoreY += frameTop;
        m_RestoreWidth -= frameLeft + frameRight;
        m_RestoreHeight -= frameTop + frameBottom;
      }

      // No monitor here makes this a plain move and resize. With one, GLFW would switch to its own
      // fullscreen window: topmost and minimized whenever it loses focus.
      glfwSetWindowMonitor(m_WindowHandle, nullptr, monitorX, monitorY, mode->width, mode->height, GLFW_DONT_CARE);

      YA_LOG_INFO("Window", "Borderless fullscreen on '%s' (%dx%d)",
        glfwGetMonitorName(monitor), mode->width, mode->height);
    }
    else
    {
      glfwSetWindowAttrib(m_WindowHandle, GLFW_DECORATED, GLFW_TRUE);
      glfwSetWindowMonitor(m_WindowHandle, nullptr, m_RestoreX, m_RestoreY,
        m_RestoreWidth, m_RestoreHeight, GLFW_DONT_CARE);
      if (b_RestoreMaximized)
        glfwMaximizeWindow(m_WindowHandle);

      YA_LOG_INFO("Window", "Windowed (%dx%d%s)",
        m_RestoreWidth, m_RestoreHeight, b_RestoreMaximized ? ", maximized" : "");
    }

    b_BorderlessFullscreen = enabled;
  }

  const std::vector<WindowEvent>& Window::PollEvents()
  {
    m_WindowEventStack.clear();
    glfwPollEvents();
    return m_WindowEventStack;
  }

  const char** Window::GetRequiredInstanceExtensions(uint32_t& extensionsCount) const
  {
    return glfwGetRequiredInstanceExtensions(&extensionsCount);
  }


}
