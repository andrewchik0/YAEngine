#pragma once

#include "Pch.h"

namespace YAEngine
{
  class VulkanCommandBuffer;

  class VulkanTexture
  {
  public:

    static void InitTextures(VkDevice device, VkQueue queue, VulkanCommandBuffer& commandBuffer, VmaAllocator allocator);

    void Load(void* data, uint32_t width, uint32_t height, uint32_t pixelSize, VkFormat format);
    void Destroy();

    VkImageView GetView()
    {
      return m_TextureImageView;
    }

    VkSampler GetSampler()
    {
      return m_TextureSampler;
    }

  private:

    VkImage m_TextureImage {};
    VmaAllocation m_TextureImageAllocation {};
    VkImageView m_TextureImageView {};
    VkSampler m_TextureSampler {};

    static VkQueue s_Queue;
    static VulkanCommandBuffer* s_CommandBuffer;
    static VmaAllocator s_Allocator;
    static VkDevice s_Device;
  };
}
