#include "VulkanSurface.h"

#include <iostream>

namespace YAEngine
{
  void VulkanSurface::Init(VkInstance instance, GLFWwindow* window)
  {
    if (!window)
      throw std::runtime_error("GLFW window not created!");

    m_Window = window;
    m_Instance = instance;

    if (VkResult result = glfwCreateWindowSurface(m_Instance, window, nullptr, &m_Surface); result != VK_SUCCESS)
    {
      std::cerr << "glfwCreateWindowSurface failed with code: " << result << std::endl;
      throw std::runtime_error("failed to create window surface!");
    }
  }

  void VulkanSurface::Destroy()
  {
    if (m_Surface != VK_NULL_HANDLE)
    {
      vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    }
  }
}
