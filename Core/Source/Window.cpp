#include "Window.h"

#include <iostream>

namespace YAEngine
{
  void Window::KeyCallback(GLFWwindow* window, int32_t key, int32_t scancode, int32_t action, int32_t mods)
  {
    auto* self = static_cast<Window*>(
      glfwGetWindowUserPointer(window)
    );

    if (self)
      self->m_WindowEventStack.push_back(std::make_unique<KeyEvent>(key, scancode, action, mods));
  }

  void Window::CursorPositionCallback(GLFWwindow* window, double xpos, double ypos)
  {
    auto* self = static_cast<Window*>(
      glfwGetWindowUserPointer(window)
    );

    if (self)
      self->m_WindowEventStack.push_back(std::make_unique<MouseMovedEvent>(xpos, ypos));
  }

  void Window::MouseButtonCallback(GLFWwindow* window, int32_t button, int32_t action, int32_t mods)
  {
    auto* self = static_cast<Window*>(
      glfwGetWindowUserPointer(window)
    );

    if (self)
      self->m_WindowEventStack.push_back(std::make_unique<MouseButtonEvent>(button, action, mods));
  }

  void Window::FramebufferSizeCallback(GLFWwindow* window, int32_t width, int32_t height)
  {
    auto* self = static_cast<Window*>(
      glfwGetWindowUserPointer(window)
    );

    if (self)
      self->m_WindowEventStack.push_back(std::make_unique<ResizeEvent>(width, height));
  }

  Window::Window(const WindowSpecs& specs)
  {
    m_WindowHeight = specs.height;
    m_WindowWidth = specs.width;

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_WindowHandle = glfwCreateWindow(m_WindowWidth, m_WindowHeight, specs.title.c_str(), nullptr, nullptr);
    if (!m_WindowHandle)
    {
      glfwTerminate();
    }
    glfwMakeContextCurrent(m_WindowHandle);
    glfwSetWindowUserPointer(m_WindowHandle, this);
    glfwSetKeyCallback(m_WindowHandle, KeyCallback);
    glfwSetCursorPosCallback(m_WindowHandle, CursorPositionCallback);
    glfwSetMouseButtonCallback(m_WindowHandle, MouseButtonCallback);
    glfwSetFramebufferSizeCallback(m_WindowHandle, FramebufferSizeCallback);
  }

  Window::~Window()
  {
    glfwDestroyWindow(m_WindowHandle);
    glfwTerminate();
  }

  bool Window::IsOpen() const
  {
    return !glfwWindowShouldClose(m_WindowHandle);
  }

  std::vector<std::unique_ptr<Event>>& Window::PollEvents()
  {
    m_WindowEventStack.clear();
    glfwPollEvents();
    return m_WindowEventStack;
  }

  const char** Window::GetRequiredInstanceExtensions(uint32_t extensionsCount) const
  {
    return glfwGetRequiredInstanceExtensions(&extensionsCount);
  }


}