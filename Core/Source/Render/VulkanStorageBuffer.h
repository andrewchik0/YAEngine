#pragma once

namespace YAEngine
{
  class VulkanStorageBuffer
  {
  public:

    static void InitBuffers(VkDevice device, VmaAllocator allocator);

    void Create(VkDeviceSize bufferSize);
    void Destroy();

    VkBuffer Get()
    {
      return m_Buffer;
    }

    template<typename T>
    void Update(uint32_t offset, const T* data, uint32_t size)
    {
      std::memcpy(static_cast<uint8_t*>(m_Data) + offset, data, size);
      vmaFlushAllocation(s_Allocator, m_Allocation, offset, size);
    }

    uint32_t Allocate(uint32_t size)
    {
      m_FilledData += size;
      return m_FilledData - size;
    }

  private:

    VkBuffer m_Buffer {};
    VmaAllocation m_Allocation {};
    VkDeviceSize m_BufferSize {};

    uint32_t m_FilledData = 0;

    static VmaAllocator s_Allocator;
    static VkDevice s_Device;

    void* m_Data {};
  };
}