#include "VulkanUniformBuffer.h"

namespace YAEngine
{
  void VulkanUniformBuffer::Create(VkDevice device, VmaAllocator allocator, VkDeviceSize bufferSize)
  {
    m_BufferSize = bufferSize;
    m_Device = device;
    m_Allocator = allocator;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &m_Buffer, &m_Allocation, nullptr) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create uniform buffer!");
    }
    vmaMapMemory(m_Allocator, m_Allocation, &m_Data);
  }

  void VulkanUniformBuffer::Destroy()
  {
    vmaUnmapMemory(m_Allocator, m_Allocation);
    vmaDestroyBuffer(m_Allocator, m_Buffer, m_Allocation);
  }
}
