#pragma once

#include "VulkanDescriptorSet.h"
#include "VulkanStorageBuffer.h"
#include "TileCullData.h"

namespace YAEngine
{
  struct RenderContext;

  class TileLightBuffer
  {
  public:

    void Init(const RenderContext& ctx, uint32_t tileCountX, uint32_t tileCountY);
    void Resize(const RenderContext& ctx, uint32_t tileCountX, uint32_t tileCountY);
    void Destroy(const RenderContext& ctx);

    VkDescriptorSetLayout GetLayout()
    {
      return m_DescriptorSets[0].GetLayout();
    }

    VkDescriptorSet GetDescriptorSet(uint32_t frameIndex)
    {
      return m_DescriptorSets[frameIndex].Get();
    }

    VkBuffer GetBuffer(uint32_t frameIndex)
    {
      return m_StorageBuffers[frameIndex].Get();
    }

    uint32_t GetTileCountX() const { return m_TileCountX; }
    uint32_t GetTileCountY() const { return m_TileCountY; }

  private:

    void CreateBuffers(const RenderContext& ctx);
    void DestroyBuffers(const RenderContext& ctx);

    std::vector<VulkanDescriptorSet> m_DescriptorSets;
    std::vector<VulkanStorageBuffer> m_StorageBuffers;
    uint32_t m_TileCountX = 0;
    uint32_t m_TileCountY = 0;
  };
}
