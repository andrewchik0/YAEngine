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

    // cameraFar is the camera's view distance, it only clamps shadowDistance - the
    // projection itself is reversed-Z with an infinite far plane, so the frustum slices
    // are built from fov/aspect in view space instead of unprojected from NDC.
    void ComputeCascades(
      const glm::mat4& cameraView,
      float cameraFov, float cameraAspect,
      float cameraNear, float cameraFar,
      float shadowDistance,
      const glm::vec3& lightDirection);

    // Cascades fitted to concentric spheres around a point instead of a camera
    // frustum. A reflection probe looks in all six directions, so one omnidirectional
    // fit serves every cube face and the atlas is rendered once per probe.
    // volumeRadius inflates every cascade sphere by that amount so a whole BOX of
    // capture points shares one fit - an irradiance volume renders the atlas once
    // for all its nodes instead of once per node. Zero reproduces the point fit.
    void ComputeCascadesAroundPoint(
      const glm::vec3& center,
      float nearPlane,
      float shadowDistance,
      const glm::vec3& lightDirection,
      float volumeRadius = 0.0f);

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
    float FitCascadeToSphere(uint32_t cascadeIndex,
      const glm::vec3& center, float radius,
      const glm::vec3& lightDir);
    float FitCascadeToFrustum(uint32_t cascadeIndex,
      const glm::mat4& invView,
      float fov, float aspect,
      float nearDist, float farDist,
      const glm::vec3& lightDir);

    static constexpr float SPLIT_LAMBDA = 0.75f;
    static constexpr uint32_t CASCADE_TILE_SIZE = SHADOW_CASCADE_SIZE;
    static constexpr float SHADOW_NEAR_PLANE = 0.01f;
    // sqrt(3): the corner-to-forward ratio of a 90 degree cube face
    static constexpr float OMNI_CASCADE_SLACK = 1.7320508f;

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
