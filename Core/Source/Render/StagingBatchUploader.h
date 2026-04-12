#pragma once

#include "Pch.h"
#include "VulkanImage.h"
#include "VulkanBuffer.h"
#include "Assets/CpuResourceData.h"

namespace YAEngine
{
  struct RenderContext;

  class StagingBatchUploader
  {
  public:

    uint32_t AddTexture(CpuTextureData&& cpuData, VkFormat format);
    uint32_t AddBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage);

    void Flush(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);
    void Clear();

    bool IsEmpty() const { return m_Textures.empty() && m_Buffers.empty(); }

    VulkanImage& GetImage(uint32_t index) { return m_Textures[index].image; }
    VulkanBuffer& GetBuffer(uint32_t index) { return m_Buffers[index].buffer; }

  private:

    struct PendingTexture
    {
      CpuTextureData cpuData;
      VulkanImage image;
      VkFormat format;
      uint32_t mipLevels;
    };

    struct PendingBuffer
    {
      std::vector<uint8_t> data;
      VulkanBuffer buffer;
      VkBufferUsageFlags usage;
    };

    std::vector<PendingTexture> m_Textures;
    std::vector<PendingBuffer> m_Buffers;
  };
}
