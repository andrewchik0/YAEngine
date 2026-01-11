#include "VulkanStorageBuffer.h"

namespace YAEngine
{
  VkDevice VulkanStorageBuffer::s_Device = nullptr;
  VmaAllocator VulkanStorageBuffer::s_Allocator = nullptr;

  void VulkanStorageBuffer::InitBuffers(VkDevice device, VmaAllocator allocator)
  {
    s_Device = device;
    s_Allocator = allocator;
  }

  void VulkanStorageBuffer::Create(VkDeviceSize bufferSize)
  {
    m_BufferSize = bufferSize;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocResult{};
    if (vmaCreateBuffer(s_Allocator, &bufferInfo, &allocInfo, &m_Buffer, &m_Allocation, &allocResult) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create buffer");
    }

    vmaMapMemory(s_Allocator, m_Allocation, &m_Data);
  }

  void VulkanStorageBuffer::Destroy()
  {
    if (m_Allocation)
    {
      vmaUnmapMemory(s_Allocator, m_Allocation);
      vmaDestroyBuffer(s_Allocator, m_Buffer, m_Allocation);
    }

    m_Buffer = VK_NULL_HANDLE;
    m_Allocation = VK_NULL_HANDLE;
  }
}