#pragma once

#include "Pch.h"

namespace YAEngine
{
  class VulkanUniformBuffer
  {
  public:

    void Create(VkDevice device, VmaAllocator allocator, VkDeviceSize bufferSize);
    void Destroy();

    VkBuffer Get()
    {
      return m_Buffer;
    }

    template<typename T>
    void Update(const T& data)
    {
      std::memcpy(m_Data, &data, m_BufferSize);
      vmaFlushAllocation(m_Allocator, m_Allocation, 0, m_BufferSize);
    }

  private:

    VkBuffer m_Buffer {};
    VmaAllocation m_Allocation {};
    VkDeviceSize m_BufferSize {};

    VmaAllocator m_Allocator {};
    VkDevice m_Device {};

    void* m_Data {};
  };
}