#pragma once

#include "Pch.h"

namespace YAEngine
{
  class VulkanCommandBuffer;

  class VulkanImGui
  {
  public:

    void Init(GLFWwindow* window, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue, uint32_t
              swapchainImageCount, uint32_t graphicsQueueFamily, VkRenderPass renderPass);
    void Destroy();

  private:

    GLFWwindow* m_Window {};
    VkDevice m_Device {};
    VkDescriptorPool m_ImGuiDescriptorPool {};
    VkPipelineCache m_PipelineCache;
  };
}