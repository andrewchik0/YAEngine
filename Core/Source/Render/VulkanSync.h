#pragma once

#include "Pch.h"

namespace YAEngine
{
  class VulkanSync
  {
  public:

    void Init(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
              uint32_t framesInFlight, uint32_t swapchainImageCount);
    void Destroy();

    bool WaitIdle(VkSwapchainKHR swapChain, uint32_t frameIndex, uint32_t* imageIndex);
    bool Submit(VkCommandBuffer commandBuffer, VkSwapchainKHR swapChain, uint32_t frameIndex, uint32_t imageIndex, bool resized);

    VkQueue& GetQueue()
    {
      return m_GraphicsQueue;
    }

  private:

    std::vector<VkSemaphore> m_ImageAvailableSemaphores;
    std::vector<VkSemaphore> m_RenderFinishedSemaphores;
    std::vector<VkFence> m_InFlightFences;

    VkQueue m_GraphicsQueue {};
    VkQueue m_PresentQueue {};

    uint32_t m_FrameCount = 0;
    uint32_t m_SemaphoreCount = 0;
    uint32_t m_SemaphoreIndex = 0;

    VkDevice m_Device {};

  };
}
