#include "VulkanSync.h"

#include "VulkanPhysicalDevice.h"

namespace YAEngine
{
  void VulkanSync::Init(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t swapChainsCount)
  {
    m_Device = device;
    m_SwapchainsCount = swapChainsCount;
    m_ImageAvailableSemaphores.resize(m_SwapchainsCount);
    m_RenderFinishedSemaphores.resize(m_SwapchainsCount);
    m_InFlightFences.resize(m_SwapchainsCount);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < m_SwapchainsCount; i++)
    {
      if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]) != VK_SUCCESS ||
          vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]) != VK_SUCCESS ||
          vkCreateFence(device, &fenceInfo, nullptr, &m_InFlightFences[i]) != VK_SUCCESS)
      {
        throw std::runtime_error("failed to create synchronization objects for a frame!");
      }
    }

    QueueFamilyIndices indices = VulkanPhysicalDevice::FindQueueFamilies(physicalDevice, surface);
    vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &m_GraphicsQueue);
    vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &m_PresentQueue);
  }

  void VulkanSync::Destroy()
  {
    vkDeviceWaitIdle(m_Device);
    for (size_t i = 0; i < m_SwapchainsCount; i++)
    {
      vkWaitForFences(m_Device, 1, &m_InFlightFences[m_FrameIndex], VK_TRUE, UINT64_MAX);
    }

    for (size_t i = 0; i < m_SwapchainsCount; i++)
    {
      vkDestroySemaphore(m_Device, m_RenderFinishedSemaphores[i], nullptr);
      vkDestroySemaphore(m_Device, m_ImageAvailableSemaphores[i], nullptr);
      vkDestroyFence(m_Device, m_InFlightFences[i], nullptr);
    }
  }

  bool VulkanSync::WaitIdle(VkSwapchainKHR swapChain, uint32_t* imageIndex)
  {
    vkWaitForFences(m_Device, 1, &m_InFlightFences[m_FrameIndex], VK_TRUE, UINT64_MAX);

    m_FrameIndex = (m_FrameIndex + 1) % m_SwapchainsCount;
    VkResult result = vkAcquireNextImageKHR(m_Device, swapChain, UINT64_MAX, m_ImageAvailableSemaphores[m_FrameIndex], VK_NULL_HANDLE, imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
      return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
      throw std::runtime_error("failed to acquire swap chain image!");
    }

    vkResetFences(m_Device, 1, &m_InFlightFences[m_FrameIndex]);
    return true;
  }

  bool VulkanSync::Submit(VkCommandBuffer commandBuffer, VkSwapchainKHR swapChain, uint32_t imageIndex, bool resized)
  {
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {m_ImageAvailableSemaphores[m_FrameIndex]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkSemaphore signalSemaphores[] = {m_RenderFinishedSemaphores[m_FrameIndex]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_InFlightFences[m_FrameIndex]) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;

    presentInfo.pImageIndices = &imageIndex;

    auto result = vkQueuePresentKHR(m_PresentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || resized)
    {
      return false;
    }
    else if (result != VK_SUCCESS)
    {
      throw std::runtime_error("failed to present swap chain image!");
    }
    return true;
  }
}
