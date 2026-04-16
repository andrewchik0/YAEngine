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

    static VulkanBuffer CreateGpuOnly(
      const RenderContext& ctx,
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
      for (size_t i = 0; i < m_FreeList.size(); i++)
      {
        auto& block = m_FreeList[i];
        if (block.size >= size)
        {
          uint32_t offset = block.offset;
          if (block.size == size)
            m_FreeList.erase(m_FreeList.begin() + i);
          else
          {
            block.offset += size;
            block.size -= size;
          }
          return offset;
        }
      }

      if (size > m_Size - m_FilledData)
        return UINT32_MAX;

      uint32_t offset = m_FilledData;
      m_FilledData += size;
      return offset;
    }

    void Free(uint32_t offset, uint32_t size)
    {
      auto it = m_FreeList.begin();
      while (it != m_FreeList.end() && it->offset < offset)
        ++it;
      it = m_FreeList.insert(it, { offset, size });

      auto next = it + 1;
      if (next != m_FreeList.end() && it->offset + it->size == next->offset)
      {
        it->size += next->size;
        m_FreeList.erase(next);
      }
      if (it != m_FreeList.begin())
      {
        auto prev = it - 1;
        if (prev->offset + prev->size == it->offset)
        {
          prev->size += it->size;
          m_FreeList.erase(it);
        }
      }

      // Roll back the bump frontier as long as the top free block abuts it,
      // so repeated regenerations with the same size do not grow m_FilledData.
      while (!m_FreeList.empty() && m_FreeList.back().offset + m_FreeList.back().size == m_FilledData)
      {
        m_FilledData = m_FreeList.back().offset;
        m_FreeList.pop_back();
      }
    }

    void ResetAllocator()
    {
      m_FilledData = 0;
      m_FreeList.clear();
    }

  private:

    struct FreeBlock
    {
      uint32_t offset;
      uint32_t size;
    };

    VkBuffer m_Buffer {};
    VmaAllocation m_Allocation {};
    VkDeviceSize m_Size {};
    void* m_MappedData {};
    uint32_t m_FilledData = 0;
    std::vector<FreeBlock> m_FreeList;
  };
}
