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

    // Reason of a refit performed by the LAST ComputeCascades call (the camera
    // path), None when every cascade reused its frozen fit. The cache skip
    // decision consults this instead of duplicating refit state. A refit never
    // records None, so the reason doubles as the "any refit" flag.
    ShadowInvalidation LastRefitReasonThisFit() const { return m_RefitReasonThisFit; }

    bool AnyRefitThisFit() const
    {
      return m_RefitReasonThisFit != ShadowInvalidation::None;
    }

    // Same question for one cascade: whether the LAST ComputeCascades call
    // refitted it. The partial rebuild redraws exactly the cascade tiles this
    // returns true for.
    bool DidCascadeRefitThisFit(uint32_t cascadeIndex) const
    {
      return b_CascadeRefitThisFit[cascadeIndex];
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

    // Set 0 of the shadow pipeline layouts. No shadow shader reads the block, so
    // this is a numbering placeholder only: nothing is ever allocated for it and
    // no set is ever bound. It exists so the shadow shaders keep their set indices.
    VkDescriptorSetLayout GetShadowCascadeUBOLayout() const { return m_CascadeLayout; }

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

    static bool FitParamDrifted(float current, float frozen);

    static constexpr float SPLIT_LAMBDA = 0.75f;
    static constexpr uint32_t CASCADE_TILE_SIZE = SHADOW_CASCADE_SIZE;
    static constexpr float SHADOW_NEAR_PLANE = 0.01f;
    // sqrt(3): the corner-to-forward ratio of a 90 degree cube face
    static constexpr float OMNI_CASCADE_SLACK = 1.7320508f;

    // Sphere inflation applied on every refit. Buys frames of reuse at the cost
    // of that factor of effective texel density.
    static constexpr float FIT_MARGIN = 1.15f;
    // Degrees the sun may drift from the frozen direction before all cascades refit.
    static constexpr float SUN_THRESHOLD_DEG = 0.5f;
    // Relative drift a fit input (shadow distance, fov, aspect, near plane) may
    // accumulate before all cascades refit. Those inputs reach nothing but the
    // frustum slice spheres, and the containment test rebuilds those from the
    // CURRENT values every frame, so a frozen matrix stays correct at any drift -
    // this bounds how far a frozen fit may lag the ideal texel density, nothing
    // more. An exact compare instead refits all four cascades on every frame an
    // input is animated, which is what a camera track's eased fov does.
    static constexpr float FIT_PARAM_THRESHOLD = 0.02f;
    // Proactive refits per frame while a cascade is close to escaping its frozen
    // sphere but still inside it.
    static constexpr int REFIT_BUDGET = 1;
    // Fit urgency at which a cascade becomes a proactive refit candidate.
    static constexpr float REFIT_SOFT_THRESHOLD = 0.95f;

    ShadowAtlas m_Atlas;
    ShadowBuffer m_ShadowData {};
    float m_CascadeSplits[CSM_CASCADE_COUNT + 1] {};

    // The fit a cascade stays on while the required sphere still fits inside it.
    // viewProj is reused VERBATIM on those frames so the matrix is bit-identical
    // between refits - what the tile cache keys on. Only ComputeCascades touches
    // this: the probe/volume bake path (ComputeCascadesAroundPoint) bypasses
    // hysteresis entirely and the next camera frame rewrites m_ShadowData from
    // here, so bakes self-heal.
    struct FrozenCascadeFit
    {
      glm::mat4 viewProj { 1.0f };
      glm::vec3 center { 0.0f };
      float radius = 0.0f;
      float texelWorldSize = 0.0f;
      bool valid = false;
    };

    FrozenCascadeFit m_FrozenFits[CSM_CASCADE_COUNT];

    // What the last ComputeCascades call refitted, and why. Reset at its entry,
    // never touched by the bake path.
    bool b_CascadeRefitThisFit[CSM_CASCADE_COUNT] {};
    ShadowInvalidation m_RefitReasonThisFit = ShadowInvalidation::None;

    // Fit inputs captured when the frozen state was established. Any change is a
    // full refit: matrices frozen against stale inputs would silently skew.
    glm::vec3 m_FrozenSunDir { 0.0f, -1.0f, 0.0f };
    float m_FrozenShadowDistance = 0.0f;
    float m_FrozenFov = 0.0f;
    float m_FrozenAspect = 0.0f;
    float m_FrozenNear = 0.0f;
    bool b_FrozenParamsValid = false;

    // Set 0 of the shadow pipeline layouts, owned by the context's layout cache.
    VkDescriptorSetLayout m_CascadeLayout = VK_NULL_HANDLE;

    // Combined shadow UBO + sampler for deferred lighting (set 2 extension)
    std::vector<VulkanDescriptorSet> m_LightingShadowDescriptorSets;
    std::vector<VulkanUniformBuffer> m_LightingShadowUBOs;
  };
}
