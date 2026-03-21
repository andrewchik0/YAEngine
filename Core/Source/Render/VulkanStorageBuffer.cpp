#include "VulkanStorageBuffer.h"

#include "RenderContext.h"

namespace YAEngine
{
  void VulkanStorageBuffer::Create(const RenderContext& ctx, VkDeviceSize bufferSize)
  {
    m_Buffer = VulkanBuffer::CreateMapped(ctx, bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  }

  void VulkanStorageBuffer::Destroy(const RenderContext& ctx)
  {
    m_Buffer.Destroy(ctx);
  }
}
