#pragma once

namespace YAEngine
{
  class VulkanSurface
  {
  public:

    void Init(VkInstance instance, GLFWwindow* window);
    void Destroy();

    VkSurfaceKHR& Get() { return m_Surface; }

  private:
    VkSurfaceKHR m_Surface {};

    VkInstance m_Instance {};
    GLFWwindow* m_Window {};
  };
}