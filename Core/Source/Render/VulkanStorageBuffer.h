#pragma once

#include "Pch.h"
#include "VulkanBuffer.h"

namespace YAEngine
{
  struct RenderContext;

  class VulkanStorageBuffer
  {
  public:

    void Create(const RenderContext& ctx, VkDeviceSize bufferSize);
    void Destroy(const RenderContext& ctx);

    VkBuffer Get() const { return m_Buffer.Get(); }

    template<typename T>
    void Update(uint32_t offset, const T* data, uint32_t size)
    {
      m_Buffer.Update(offset, data, size);
    }

    uint32_t Allocate(uint32_t size)
    {
      return m_Buffer.Allocate(size);
    }

  private:

    VulkanBuffer m_Buffer;
  };
}
