#include "LightProbeStorageBuffer.h"

#include "RenderContext.h"

namespace YAEngine
{
  void LightProbeStorageBuffer::Init(const RenderContext& ctx)
  {
    m_StorageBuffers.resize(ctx.maxFramesInFlight);

    for (size_t i = 0; i < ctx.maxFramesInFlight; i++)
      m_StorageBuffers[i].Create(ctx, sizeof(LightProbeBuffer));
  }

  void LightProbeStorageBuffer::Destroy(const RenderContext& ctx)
  {
    for (auto& ssbo : m_StorageBuffers)
      ssbo.Destroy(ctx);
  }

  void LightProbeStorageBuffer::SetUp(uint32_t frameIndex, const LightProbeBuffer& data)
  {
    uint32_t headerSize = offsetof(LightProbeBuffer, probes);
    uint32_t probesSize = data.probeCount * uint32_t(sizeof(LightProbeInfo));

    m_StorageBuffers[frameIndex].Update(0u, &data, headerSize + probesSize);
  }
}
