#pragma once

#include "Pch.h"

namespace YAEngine
{
  class VulkanCommandBuffer;
  struct RenderContext;

  class VulkanBuffer
  {
  public:

    static VulkanBuffer CreateStaged(
      const RenderContext& ctx,
      const void* data,
      VkDeviceSize size,
      VkBufferUsageFlags usage);

    static VulkanBuffer CreateMapped(
      const RenderContext& ctx,
      VkDeviceSize size,
      VkBufferUsageFlags usage);

    static VulkanBuffer CreateReadback(
      const RenderContext& ctx,
      VkDeviceSize size);

    void Destroy(const RenderContext& ctx);

    VkBuffer Get() const
    {
      return m_Buffer;
    }

    VkDeviceSize GetSize() const
    {
      return m_Size;
    }

    void Update(uint32_t offset, const void* data, uint32_t size)
    {
      std::memcpy(static_cast<uint8_t*>(m_MappedData) + offset, data, size);
    }

    void* GetMapped() const { return m_MappedData; }

    uint32_t Allocate(uint32_t size)
    {
      assert(m_FilledData + size <= m_Size && "VulkanBuffer::Allocate: out of buffer space");
      uint32_t offset = m_FilledData;
      m_FilledData += size;
      return offset;
    }

    void ResetAllocator()
    {
      m_FilledData = 0;
    }

  private:

    VkBuffer m_Buffer {};
    VmaAllocation m_Allocation {};
    VkDeviceSize m_Size {};
    void* m_MappedData {};
    uint32_t m_FilledData = 0;
  };
}
