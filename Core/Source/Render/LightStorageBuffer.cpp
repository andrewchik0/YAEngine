#include "LightStorageBuffer.h"

#include "RenderContext.h"

namespace YAEngine
{
  void LightStorageBuffer::Init(const RenderContext& ctx)
  {
    m_DescriptorSets.resize(ctx.maxFramesInFlight);
    m_StorageBuffers.resize(ctx.maxFramesInFlight);

    VkDescriptorSetLayout layout = nullptr;
    for (size_t i = 0; i < ctx.maxFramesInFlight; i++)
    {
      SetDescription desc = {
        .set = 0,
        .bindings = {
          {
            { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT }
          }
        }
      };
      if (i == 0)
      {
        m_DescriptorSets[i].Init(ctx, desc);
        layout = m_DescriptorSets[i].GetLayout();
      }
      else
      {
        m_DescriptorSets[i].Init(ctx, layout);
      }
      m_StorageBuffers[i].Create(ctx, sizeof(LightBuffer));
      m_DescriptorSets[i].WriteStorageBuffer(0, m_StorageBuffers[i].Get(), sizeof(LightBuffer));
    }
  }

  void LightStorageBuffer::Destroy(const RenderContext& ctx)
  {
    for (auto& set : m_DescriptorSets)
      set.Destroy();
    for (auto& ssbo : m_StorageBuffers)
      ssbo.Destroy(ctx);
  }

  void LightStorageBuffer::SetUp(uint32_t frameIndex, const LightBuffer& data)
  {
    // Upload only the header + actually used lights instead of the full 16KB buffer
    uint32_t headerSize = offsetof(LightBuffer, pointLights);
    uint32_t pointSize = data.pointLightCount * uint32_t(sizeof(PointLight));
    uint32_t spotOffset = headerSize + MAX_POINT_LIGHTS * uint32_t(sizeof(PointLight));
    uint32_t spotSize = data.spotLightCount * uint32_t(sizeof(SpotLight));

    // Always upload header (directional + counts)
    m_StorageBuffers[frameIndex].Update(0u, &data, headerSize);

    if (data.pointLightCount > 0)
      m_StorageBuffers[frameIndex].Update(headerSize, data.pointLights, pointSize);

    if (data.spotLightCount > 0)
      m_StorageBuffers[frameIndex].Update(spotOffset, data.spotLights, spotSize);
  }
}
