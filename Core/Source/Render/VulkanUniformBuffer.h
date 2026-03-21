#pragma once

#include "Pch.h"
#include "VulkanBuffer.h"

namespace YAEngine
{
  struct RenderContext;

  class VulkanUniformBuffer
  {
  public:

    void Create(const RenderContext& ctx, VkDeviceSize bufferSize);
    void Destroy(const RenderContext& ctx);

    VkBuffer Get() const { return m_Buffer.Get(); }

    template<typename T>
    void Update(const T& data)
    {
      m_Buffer.Update(0, &data, static_cast<uint32_t>(m_BufferSize));
    }

  private:

    VulkanBuffer m_Buffer;
    VkDeviceSize m_BufferSize {};
  };
}
