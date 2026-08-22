#include "ReflectionProbeStorageBuffer.h"

#include "RenderContext.h"

namespace YAEngine
{
  void ReflectionProbeStorageBuffer::Init(const RenderContext& ctx)
  {
    m_StorageBuffers.resize(ctx.maxFramesInFlight);

    for (size_t i = 0; i < ctx.maxFramesInFlight; i++)
      m_StorageBuffers[i].Create(ctx, sizeof(ReflectionProbeBuffer));
  }

  void ReflectionProbeStorageBuffer::Destroy(const RenderContext& ctx)
  {
    for (auto& ssbo : m_StorageBuffers)
      ssbo.Destroy(ctx);
  }

  void ReflectionProbeStorageBuffer::SetUp(uint32_t frameIndex, const ReflectionProbeBuffer& data)
  {
    uint32_t headerSize = offsetof(ReflectionProbeBuffer, probes);
    uint32_t probesSize = data.probeCount * uint32_t(sizeof(ReflectionProbeInfo));

    m_StorageBuffers[frameIndex].Update(0u, &data, headerSize + probesSize);
  }
}
