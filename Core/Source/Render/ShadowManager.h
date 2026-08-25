#pragma once

#include "ShadowAtlas.h"
#include "VulkanDescriptorSet.h"
#include "VulkanUniformBuffer.h"
#include "ShadowData.h"
#include "ShadowInvalidation.h"

namespace YAEngine
{
  struct RenderContext;
  struct SceneSnapshot;

  // Refit instrumentation for one cascade - the Stage 2 measurable: refit
  // frequency under camera motion predicts the future tile cache's ceiling.
  struct CascadeFitStats
  {
    uint32_t refitCount = 0;
    uint64_t lastRefitFrame = 0;
    double lastRefitTime = 0.0;
    ShadowInvalidation lastReason = ShadowInvalidation::None;
  };

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

    // Applies to the next ComputeCascades call. With hysteresis on, each cascade
    // keeps its frozen light matrix bit-identical until the required frustum
    // sphere escapes the frozen one - the future tile cache keys on that.
    // refitBudget caps stage 5 SCHEDULED (proactive) refits per frame; 0
    // disables scheduling, which is bit-exact stage 4 behavior (the A/B
    // control). refitSoftThreshold is the fit urgency at which a cascade
    // becomes a proactive refit candidate; ComputeCascades clamps it above
    // 1/margin at use time.
    void SetFitHysteresis(bool enabled, float margin, float sunThresholdDeg,
      int refitBudget, float refitSoftThreshold)
    {
      b_FitHysteresis = enabled;
      m_FitMargin = margin;
      m_SunThresholdDeg = sunThresholdDeg;
      m_RefitBudget = refitBudget;
      m_RefitSoftThreshold = refitSoftThreshold;
    }

    const CascadeFitStats& GetCascadeFitStats(uint32_t cascadeIndex) const
    {
      return m_FitStats[cascadeIndex];
    }

    // Fit urgency of a cascade after the last ComputeCascades call: (distance
    // from the frozen center plus the required radius) over the frozen radius.
    // 1.0 is the escape boundary; right after a refit it sits near 1/margin.
    float GetCascadeUrgency(uint32_t cascadeIndex) const
    {
      return m_CascadeUrgency[cascadeIndex];
    }

    // Lifetime split of organic cascade refits for the UI. Scheduled: refits
    // the stage 5 scheduler performed early, while the cascade still fit its
    // frozen sphere. Forced: hard escapes (urgency >= 1) the scheduler failed
    // to preempt. Full-refit reasons (sun, params, toggle, cold start) count
    // in neither, so forced cleanly answers "did the scheduler fall behind".
    uint64_t GetScheduledRefitCount() const { return m_ScheduledRefitCount; }
    uint64_t GetForcedRefitCount() const { return m_ForcedRefitCount; }

    // Reason of a refit performed by the LAST ComputeCascades call (the camera
    // path), None when every cascade reused its frozen fit. The stage 3 skip
    // decision consults this instead of duplicating refit state. A refit never
    // records None, so the reason doubles as the "any refit" flag.
    ShadowInvalidation LastRefitReasonThisFit() const
    {
      for (const auto& stats : m_FitStats)
      {
        if (stats.refitCount > 0 && stats.lastRefitFrame == m_FitFrame)
          return stats.lastReason;
      }
      return ShadowInvalidation::None;
    }

    bool AnyRefitThisFit() const
    {
      return LastRefitReasonThisFit() != ShadowInvalidation::None;
    }

    // Same comparison for one cascade: whether the LAST ComputeCascades call
    // refitted it. The stage 4 partial rebuild redraws exactly the cascade
    // tiles this returns true for.
    bool DidCascadeRefitThisFit(uint32_t cascadeIndex) const
    {
      const auto& stats = m_FitStats[cascadeIndex];
      return stats.refitCount > 0 && stats.lastRefitFrame == m_FitFrame;
    }

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
    static void ComputeFrustumSliceSphere(const glm::mat4& invView,
      float fov, float aspect,
      float nearDist, float farDist,
      glm::vec3& outCenter, float& outRadius);

    static constexpr float SPLIT_LAMBDA = 0.75f;
    static constexpr uint32_t CASCADE_TILE_SIZE = SHADOW_CASCADE_SIZE;
    static constexpr float SHADOW_NEAR_PLANE = 0.01f;
    // sqrt(3): the corner-to-forward ratio of a 90 degree cube face
    static constexpr float OMNI_CASCADE_SLACK = 1.7320508f;

    ShadowAtlas m_Atlas;
    ShadowBuffer m_ShadowData {};
    float m_CascadeSplits[CSM_CASCADE_COUNT + 1] {};

    // The fit a cascade stays on while the required sphere still fits inside it.
    // viewProj is reused VERBATIM on those frames so the matrix is bit-identical
    // between refits. Only ComputeCascades touches this - the probe/volume bake
    // path (ComputeCascadesAroundPoint) bypasses hysteresis entirely and the
    // next camera frame rewrites m_ShadowData from here, so bakes self-heal.
    struct FrozenCascadeFit
    {
      glm::mat4 viewProj { 1.0f };
      glm::vec3 center { 0.0f };
      float radius = 0.0f;
      float texelWorldSize = 0.0f;
      bool valid = false;
    };

    FrozenCascadeFit m_FrozenFits[CSM_CASCADE_COUNT];
    CascadeFitStats m_FitStats[CSM_CASCADE_COUNT];

    bool b_FitHysteresis = true;
    float m_FitMargin = 1.15f;
    float m_SunThresholdDeg = 0.5f;
    // Stage 5 refit scheduler knobs, applied by ComputeCascades. Budget 0
    // disables proactive refits entirely (stage 4 A/B control).
    int m_RefitBudget = 1;
    float m_RefitSoftThreshold = 0.95f;
    float m_CascadeUrgency[CSM_CASCADE_COUNT] {};
    uint64_t m_ScheduledRefitCount = 0;
    uint64_t m_ForcedRefitCount = 0;

    // Fit inputs captured when the frozen state was established. Any change is a
    // full refit: matrices frozen against stale inputs would silently skew.
    glm::vec3 m_FrozenSunDir { 0.0f, -1.0f, 0.0f };
    float m_FrozenShadowDistance = 0.0f;
    float m_FrozenFov = 0.0f;
    float m_FrozenAspect = 0.0f;
    float m_FrozenNear = 0.0f;
    bool b_FrozenParamsValid = false;
    // Set while hysteresis is off so switching it back on refits everything with
    // its own reason instead of silently reusing pre-toggle state.
    bool b_HysteresisWasOff = false;
    // Counts ComputeCascades calls (the camera path only), so refit stats can
    // name the frame without the manager knowing about the engine frame index.
    uint64_t m_FitFrame = 0;

    // Per-frame UBO for shadow cascade data (used in shadow vertex shader set 0)
    std::vector<VulkanDescriptorSet> m_CascadeDescriptorSets;
    std::vector<VulkanUniformBuffer> m_CascadeUBOs;

    // Combined shadow UBO + sampler for deferred lighting (set 2 extension)
    std::vector<VulkanDescriptorSet> m_LightingShadowDescriptorSets;
    std::vector<VulkanUniformBuffer> m_LightingShadowUBOs;
  };
}
