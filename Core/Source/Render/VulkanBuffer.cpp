#include "VulkanBuffer.h"

#include "RenderContext.h"
#include "VulkanCommandBuffer.h"
#include "Utils/Log.h"

namespace YAEngine
{
  VulkanBuffer VulkanBuffer::CreateStaged(
    const RenderContext& ctx,
    const void* data,
    VkDeviceSize size,
    VkBufferUsageFlags usage)
  {
    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = size;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;

    if (vmaCreateBuffer(ctx.allocator, &stagingInfo, &stagingAllocInfo, &stagingBuffer, &stagingAlloc, nullptr) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create staging buffer");
      throw std::runtime_error("Failed to create staging buffer");
    }

    void* mapped;
    vmaMapMemory(ctx.allocator, stagingAlloc, &mapped);
    memcpy(mapped, data, (size_t)size);
    vmaUnmapMemory(ctx.allocator, stagingAlloc);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VulkanBuffer result;
    result.m_Size = size;

    if (vmaCreateBuffer(ctx.allocator, &bufferInfo, &allocInfo, &result.m_Buffer, &result.m_Allocation, nullptr) != VK_SUCCESS)
    {
      vmaDestroyBuffer(ctx.allocator, stagingBuffer, stagingAlloc);
      YA_LOG_ERROR("Render", "Failed to create GPU buffer");
      throw std::runtime_error("Failed to create GPU buffer");
    }

    VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd, stagingBuffer, result.m_Buffer, 1, &copyRegion);

    ctx.commandBuffer->EndSingleTimeCommands(cmd);
    vmaDestroyBuffer(ctx.allocator, stagingBuffer, stagingAlloc);

    return result;
  }

  VulkanBuffer VulkanBuffer::CreateMapped(
    const RenderContext& ctx,
    VkDeviceSize size,
    VkBufferUsageFlags usage)
  {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags =
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
      VMA_ALLOCATION_CREATE_MAPPED_BIT;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    VulkanBuffer result;
    result.m_Size = size;

    VmaAllocationInfo resultInfo{};
    if (vmaCreateBuffer(ctx.allocator, &bufferInfo, &allocInfo, &result.m_Buffer, &result.m_Allocation, &resultInfo) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create mapped buffer");
      throw std::runtime_error("Failed to create mapped buffer");
    }

    result.m_MappedData = resultInfo.pMappedData;
    return result;
  }

  VulkanBuffer VulkanBuffer::CreateReadback(
    const RenderContext& ctx,
    VkDeviceSize size)
  {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    VulkanBuffer result;
    result.m_Size = size;

    VmaAllocationInfo resultInfo{};
    if (vmaCreateBuffer(ctx.allocator, &bufferInfo, &allocInfo, &result.m_Buffer, &result.m_Allocation, &resultInfo) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create readback buffer");
      throw std::runtime_error("Failed to create readback buffer");
    }

    result.m_MappedData = resultInfo.pMappedData;
    return result;
  }

  void VulkanBuffer::Destroy(const RenderContext& ctx)
  {
    if (m_Allocation)
    {
      m_MappedData = nullptr;
      vmaDestroyBuffer(ctx.allocator, m_Buffer, m_Allocation);
    }
    m_Buffer = VK_NULL_HANDLE;
    m_Allocation = VK_NULL_HANDLE;
  }
}
