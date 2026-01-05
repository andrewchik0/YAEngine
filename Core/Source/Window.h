#pragma once

#include "Events.h"
#include "Pch.h"

struct GLFWwindow;

namespace YAEngine
{

  struct WindowSpecs
  {
    uint32_t width = 800, height = 600;
    std::string title = "YAEngine";
  };

  class Window
  {
  public:
    explicit Window(const WindowSpecs& specs);
    void Destroy();

    bool IsOpen() const;

    std::vector<std::unique_ptr<Event>>& PollEvents();

    [[nodiscard]] const char** GetRequiredInstanceExtensions(uint32_t extensionsCount) const;

    GLFWwindow* Get()
    {
      return m_WindowHandle;
    }

    uint32_t GetWidth() const
    {
      return m_WindowWidth;
    }

    uint32_t GetHeight() const
    {
      return m_WindowHeight;
    }

  private:

    GLFWwindow* m_WindowHandle = nullptr;
    uint32_t m_WindowWidth = 0, m_WindowHeight = 0;

    std::vector<std::unique_ptr<Event>> m_WindowEventStack;

    static void KeyCallback(GLFWwindow* window, int32_t key, int32_t scancode, int32_t action, int32_t mods);
    static void CursorPositionCallback(GLFWwindow* window, double xpos, double ypos);
    static void MouseButtonCallback(GLFWwindow* window, int32_t button, int32_t action, int32_t mods);
    static void FramebufferSizeCallback(GLFWwindow* window, int32_t width, int32_t height);
  };
}
