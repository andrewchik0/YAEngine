#include "VulkanSync.h"

#include "VulkanPhysicalDevice.h"
#include "Utils/Log.h"

namespace YAEngine
{
  void VulkanSync::Init(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                        uint32_t framesInFlight, uint32_t swapchainImageCount)
  {
    m_Device = device;
    m_FrameCount = framesInFlight;
    m_SemaphoreCount = swapchainImageCount;
    m_SemaphoreIndex = 0;

    m_ImageAvailableSemaphores.resize(m_SemaphoreCount);
    m_RenderFinishedSemaphores.resize(m_SemaphoreCount);
    m_InFlightFences.resize(m_FrameCount);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (size_t i = 0; i < m_SemaphoreCount; i++)
    {
      if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]) != VK_SUCCESS ||
          vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]) != VK_SUCCESS)
      {
        YA_LOG_ERROR("Render", "Failed to create semaphores for index %zu", i);
        throw std::runtime_error("failed to create synchronization objects for a frame!");
      }
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < m_FrameCount; i++)
    {
      if (vkCreateFence(device, &fenceInfo, nullptr, &m_InFlightFences[i]) != VK_SUCCESS)
      {
        YA_LOG_ERROR("Render", "Failed to create fence for frame %zu", i);
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
    for (size_t i = 0; i < m_FrameCount; i++)
    {
      vkWaitForFences(m_Device, 1, &m_InFlightFences[i], VK_TRUE, UINT64_MAX);
      vkDestroyFence(m_Device, m_InFlightFences[i], nullptr);
    }

    for (size_t i = 0; i < m_SemaphoreCount; i++)
    {
      vkDestroySemaphore(m_Device, m_RenderFinishedSemaphores[i], nullptr);
      vkDestroySemaphore(m_Device, m_ImageAvailableSemaphores[i], nullptr);
    }
  }

  bool VulkanSync::WaitIdle(VkSwapchainKHR swapChain, uint32_t frameIndex, uint32_t* imageIndex)
  {
    vkWaitForFences(m_Device, 1, &m_InFlightFences[frameIndex], VK_TRUE, UINT64_MAX);

    m_SemaphoreIndex = (m_SemaphoreIndex + 1) % m_SemaphoreCount;

    VkResult result = vkAcquireNextImageKHR(m_Device, swapChain, UINT64_MAX, m_ImageAvailableSemaphores[m_SemaphoreIndex], VK_NULL_HANDLE, imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
      return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
      YA_LOG_ERROR("Render", "Failed to acquire swap chain image, VkResult = %d", result);
      throw std::runtime_error("failed to acquire swap chain image!");
    }

    vkResetFences(m_Device, 1, &m_InFlightFences[frameIndex]);
    return true;
  }

  bool VulkanSync::Submit(VkCommandBuffer commandBuffer, VkSwapchainKHR swapChain, uint32_t frameIndex, uint32_t imageIndex, bool resized)
  {
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {m_ImageAvailableSemaphores[m_SemaphoreIndex]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkSemaphore signalSemaphores[] = {m_RenderFinishedSemaphores[m_SemaphoreIndex]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_InFlightFences[frameIndex]) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to submit draw command buffer");
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
      YA_LOG_ERROR("Render", "Failed to present swap chain image, VkResult = %d", result);
      throw std::runtime_error("failed to present swap chain image!");
    }
    return true;
  }
}
