#include "VulkanCommandBuffer.h"

#include "VulkanPhysicalDevice.h"
#include "Log.h"

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
      YA_LOG_ERROR("Render", "Failed to create command pool");
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
      YA_LOG_ERROR("Render", "Failed to allocate command buffers");
      throw std::runtime_error("failed to allocate command buffers!");
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(m_Device, &fenceInfo, nullptr, &m_SingleTimeFence) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create single-time fence");
      throw std::runtime_error("Failed to create single-time fence!");
    }
  }

  void VulkanCommandBuffer::Destroy()
  {
    vkDestroyFence(m_Device, m_SingleTimeFence, nullptr);
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
      YA_LOG_ERROR("Render", "Failed to begin recording command buffer");
      throw std::runtime_error("failed to begin recording command buffer!");
    }
  }

  void VulkanCommandBuffer::End(size_t currentFrameIndex)
  {
    if (vkEndCommandBuffer(m_CommandBuffers[currentFrameIndex]) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to end recording command buffer");
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
    VkResult result = vkAllocateCommandBuffers(m_Device, &allocInfo, &commandBuffer);
    if (result != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to allocate single-time command buffer: %d", result);
      throw std::runtime_error("Failed to allocate single-time command buffer");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    if (result != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to begin single-time command buffer: %d", result);
      throw std::runtime_error("Failed to begin single-time command buffer");
    }

    return commandBuffer;
  }

  void VulkanCommandBuffer::EndSingleTimeCommands(VkCommandBuffer commandBuffer)
  {
    vkEndCommandBuffer(commandBuffer);

    vkResetFences(m_Device, 1, &m_SingleTimeFence);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkResult result = vkQueueSubmit(m_Queue, 1, &submitInfo, m_SingleTimeFence);
    if (result != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to submit single-time command buffer: %d", result);
      throw std::runtime_error("Failed to submit single-time command buffer");
    }
    vkWaitForFences(m_Device, 1, &m_SingleTimeFence, VK_TRUE, UINT64_MAX);

    vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &commandBuffer);
  }
}
