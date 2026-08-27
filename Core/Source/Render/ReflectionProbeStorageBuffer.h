#pragma once

#include "VulkanStorageBuffer.h"
#include "ReflectionProbeData.h"

namespace YAEngine
{
  static_assert(sizeof(ReflectionProbeInfo) == 96, "ReflectionProbeInfo no longer matches its std430 layout");
  static_assert(offsetof(ReflectionProbeBuffer, probes) == 16, "ReflectionProbeBuffer array must start on a 16 byte boundary");

  struct RenderContext;

  class ReflectionProbeStorageBuffer
  {
  public:

    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    void SetUp(uint32_t frameIndex, const ReflectionProbeBuffer& data);

    VkBuffer GetBuffer(uint32_t frameIndex)
    {
      return m_StorageBuffers[frameIndex].Get();
    }

  private:

    std::vector<VulkanStorageBuffer> m_StorageBuffers;
  };
}
