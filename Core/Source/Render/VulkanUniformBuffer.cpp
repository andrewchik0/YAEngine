#include "VulkanUniformBuffer.h"

#include "RenderContext.h"

namespace YAEngine
{
  void VulkanUniformBuffer::Create(const RenderContext& ctx, VkDeviceSize bufferSize)
  {
    m_BufferSize = bufferSize;
    m_Buffer = VulkanBuffer::CreateMapped(ctx, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
  }

  void VulkanUniformBuffer::Destroy(const RenderContext& ctx)
  {
    m_Buffer.Destroy(ctx);
  }
}
