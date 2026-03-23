#include "VulkanSurface.h"

#include "Log.h"

namespace YAEngine
{
  void VulkanSurface::Init(VkInstance instance, GLFWwindow* window)
  {
    if (!window)
    {
      YA_LOG_ERROR("Render", "GLFW window not created");
      throw std::runtime_error("GLFW window not created!");
    }

    m_Window = window;
    m_Instance = instance;

    if (VkResult result = glfwCreateWindowSurface(m_Instance, window, nullptr, &m_Surface); result != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "glfwCreateWindowSurface failed with code: %d", result);
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
