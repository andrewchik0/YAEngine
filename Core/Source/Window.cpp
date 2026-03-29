#include "Window.h"
#include "Utils/Log.h"

namespace YAEngine
{
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
