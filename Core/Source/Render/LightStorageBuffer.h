#pragma once

#include "VulkanDescriptorSet.h"
#include "VulkanStorageBuffer.h"
#include "LightData.h"

namespace YAEngine
{
  struct RenderContext;

  // std430: every light struct is made of vec4 only, so they stride 48, 64 and 32. The four ints
  // after the directional light fill 32-47, which puts the point light array at 48.
  static_assert(sizeof(DirectionalLight) == 32 && sizeof(PointLight) == 48 && sizeof(SpotLight) == 64
    && offsetof(LightBuffer, directionalFlags) == 40 && offsetof(LightBuffer, pointLights) == 48
    && sizeof(LightBuffer) == 48 + 48 * MAX_POINT_LIGHTS + 64 * MAX_SPOT_LIGHTS,
    "LightBuffer no longer matches its std430 layout");

  // The span of the path tracer's flattened light candidates - 0 the sun, then the point lights, then
  // the spots - that holds every sphere light (a source radius, not raster only), see
  // PathTraceConstants::sphereLightBegin. Empty, begin == end, where there is none.
  struct SphereLightSpan
  {
    int32_t begin = 0;
    int32_t end = 0;
  };

  SphereLightSpan FindSphereLightSpan(const LightBuffer& lights);

  class LightStorageBuffer
  {
  public:

    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    void SetUp(uint32_t frameIndex, const LightBuffer& data);

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

  private:

    std::vector<VulkanDescriptorSet> m_DescriptorSets;
    std::vector<VulkanStorageBuffer> m_StorageBuffers;
  };
}
