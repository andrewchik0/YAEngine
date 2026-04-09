#pragma once

#include "VulkanStorageBuffer.h"
#include "LightProbeData.h"

namespace YAEngine
{
  struct RenderContext;

  class LightProbeStorageBuffer
  {
  public:

    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    void SetUp(uint32_t frameIndex, const LightProbeBuffer& data);

    VkBuffer GetBuffer(uint32_t frameIndex)
    {
      return m_StorageBuffers[frameIndex].Get();
    }

  private:

    std::vector<VulkanStorageBuffer> m_StorageBuffers;
  };
}
