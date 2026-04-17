#pragma once

#include "ShadowAtlas.h"
#include "VulkanDescriptorSet.h"
#include "VulkanUniformBuffer.h"
#include "ShadowData.h"

namespace YAEngine
{
  struct RenderContext;
  struct SceneSnapshot;

  class ShadowManager
  {
  public:

    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    void ComputeCascades(
      const glm::mat4& cameraView,
      const glm::mat4& cameraProj,
      float cameraNear, float cameraFar,
      float shadowDistance,
      const glm::vec3& lightDirection);

    void ComputeSpotShadow(uint32_t spotIndex,
      const glm::vec3& position,
      const glm::vec3& direction,
      float outerCone, float radius);

    void ComputePointShadow(uint32_t pointIndex,
      const glm::vec3& position, float radius);

    void SetUp(uint32_t frameIndex);

    const ShadowAtlas& GetAtlas() const { return m_Atlas; }
    const ShadowBuffer& GetShadowData() const { return m_ShadowData; }

    VkDescriptorSetLayout GetShadowCascadeUBOLayout() const { return m_CascadeDescriptorSets[0].GetLayout(); }
    VkDescriptorSet GetShadowCascadeUBODescriptorSet(uint32_t frameIndex) const { return m_CascadeDescriptorSets[frameIndex].Get(); }

    VkDescriptorSet GetLightingShadowDescriptorSet(uint32_t frameIndex) const { return m_LightingShadowDescriptorSets[frameIndex].Get(); }
    VkDescriptorSetLayout GetLightingShadowLayout() const { return m_LightingShadowDescriptorSets[0].GetLayout(); }

    VkBuffer GetShadowUBOBuffer(uint32_t frameIndex) const { return m_LightingShadowUBOs[frameIndex].Get(); }

    bool IsEnabled() const { return m_ShadowData.shadowsEnabled != 0; }
    void SetEnabled(bool enabled) { m_ShadowData.shadowsEnabled = enabled ? 1 : 0; }
    void SetSpotShadowCount(int count) { m_ShadowData.spotShadowCount = count; }
    void SetPointShadowCount(int count) { m_ShadowData.pointShadowCount = count; }

  private:

    void ComputeCascadeSplits(float nearPlane, float shadowDistance);
    float FitCascadeToFrustum(uint32_t cascadeIndex,
      const glm::mat4& invViewProj,
      float nearSplit, float farSplit,
      const glm::vec3& lightDir);

    static constexpr float SPLIT_LAMBDA = 0.75f;
    static constexpr uint32_t CASCADE_TILE_SIZE = SHADOW_CASCADE_SIZE;
    static constexpr float SHADOW_NEAR_PLANE = 0.01f;

    ShadowAtlas m_Atlas;
    ShadowBuffer m_ShadowData {};
    float m_CascadeSplits[CSM_CASCADE_COUNT + 1] {};

    // Per-frame UBO for shadow cascade data (used in shadow vertex shader set 0)
    std::vector<VulkanDescriptorSet> m_CascadeDescriptorSets;
    std::vector<VulkanUniformBuffer> m_CascadeUBOs;

    // Combined shadow UBO + sampler for deferred lighting (set 2 extension)
    std::vector<VulkanDescriptorSet> m_LightingShadowDescriptorSets;
    std::vector<VulkanUniformBuffer> m_LightingShadowUBOs;
  };
}
