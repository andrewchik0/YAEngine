#include "VulkanCommandBuffer.h"

#include "VulkanPhysicalDevice.h"

namespace YAEngine
{
  void VulkanCommandBuffer::Init(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t maxFramesInFlight)
  {
    m_Device = device;

    QueueFamilyIndices queueFamilyIndices = VulkanPhysicalDevice::FindQueueFamilies(physicalDevice, surface);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    if (vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create command pool!");
    }

    m_CommandBuffers.resize(maxFramesInFlight);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_CommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t) m_CommandBuffers.size();

    if (vkAllocateCommandBuffers(m_Device, &allocInfo, m_CommandBuffers.data()) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to allocate command buffers!");
    }
  }

  void VulkanCommandBuffer::Destroy()
  {
    vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
  }

  void VulkanCommandBuffer::SetGraphicsQueue(VkQueue queue)
  {
    m_Queue = queue;
  }

  void VulkanCommandBuffer::Set(size_t currentFrameIndex)
  {
    m_CurrentFrameIndex = (uint32_t)currentFrameIndex;
    vkResetCommandBuffer(m_CommandBuffers[currentFrameIndex], /*VkCommandBufferResetFlagBits*/ 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(m_CommandBuffers[currentFrameIndex], &beginInfo) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to begin recording command buffer!");
    }
  }

  void VulkanCommandBuffer::End(size_t currentFrameIndex)
  {
    if (vkEndCommandBuffer(m_CommandBuffers[currentFrameIndex]) != VK_SUCCESS)
      {
      throw std::runtime_error("failed to record command buffer!");
    }
  }

  VkCommandBuffer VulkanCommandBuffer::BeginSingleTimeCommands()
  {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_CommandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_Device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
  }

  void VulkanCommandBuffer::EndSingleTimeCommands(VkCommandBuffer commandBuffer)
  {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_Queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_Queue);

    vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &commandBuffer);
  }
}
