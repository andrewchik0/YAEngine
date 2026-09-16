#include "LightStorageBuffer.h"

#include "RenderContext.h"

namespace YAEngine
{
  SphereLightSpan FindSphereLightSpan(const LightBuffer& lights)
  {
    const int32_t pointCount = std::clamp(lights.pointLightCount, 0, MAX_POINT_LIGHTS);
    const int32_t spotCount = std::clamp(lights.spotLightCount, 0, MAX_SPOT_LIGHTS);

    SphereLightSpan span;
    auto include = [&span](int32_t candidate) {
      if (span.begin == span.end)
        span.begin = candidate;
      span.end = candidate + 1;
    };

    for (int32_t i = 0; i < pointCount; i++)
    {
      const glm::vec4& shadowPad = lights.pointLights[i].shadowPad;
      if (shadowPad.y > 0.0f && shadowPad.z <= 0.5f)
        include(1 + i);
    }
    for (int32_t i = 0; i < spotCount; i++)
    {
      const glm::vec4& intensityShadow = lights.spotLights[i].intensityShadow;
      if (intensityShadow.z > 0.0f && intensityShadow.w <= 0.5f)
        include(1 + pointCount + i);
    }
    return span;
  }

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

    m_StorageBuffers[frameIndex].Update(0u, &data, headerSize);

    if (data.pointLightCount > 0)
      m_StorageBuffers[frameIndex].Update(headerSize, data.pointLights, pointSize);

    if (data.spotLightCount > 0)
      m_StorageBuffers[frameIndex].Update(spotOffset, data.spotLights, spotSize);
  }
}
