#include "ShadowManager.h"
#include "DescriptorLayoutCache.h"
#include "RenderContext.h"
#include "Utils/Log.h"

namespace YAEngine
{
  void ShadowManager::Init(const RenderContext& ctx)
  {
    m_Atlas.Init(ctx);

    uint32_t framesInFlight = ctx.maxFramesInFlight;

    // Set 0 of every shadow pipeline layout. No shadow shader reads it - the tile
    // projection is folded into the model matrix on the CPU - so only the LAYOUT
    // exists, keeping the shadow shaders' set indices as they are. Nothing is
    // allocated for it and no set is ever bound. The cache owns the layout.
    m_CascadeLayout = ctx.layoutCache->GetOrCreate(ctx.device,
      { { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT } });

    m_LightingShadowDescriptorSets.resize(framesInFlight);
    m_LightingShadowUBOs.resize(framesInFlight);

    VkDescriptorSetLayout lightingShadowLayout = nullptr;
    for (uint32_t i = 0; i < framesInFlight; i++)
    {
      SetDescription lsDesc = {
        .set = 0,
        .bindings = {
          {
            { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          }
        }
      };
      if (i == 0)
      {
        m_LightingShadowDescriptorSets[i].Init(ctx, lsDesc);
        lightingShadowLayout = m_LightingShadowDescriptorSets[i].GetLayout();
      }
      else
      {
        m_LightingShadowDescriptorSets[i].Init(ctx, lightingShadowLayout);
      }
      m_LightingShadowUBOs[i].Create(ctx, sizeof(ShadowBuffer));
      m_LightingShadowDescriptorSets[i].WriteUniformBuffer(0, m_LightingShadowUBOs[i].Get(), sizeof(ShadowBuffer));
      m_LightingShadowDescriptorSets[i].WriteCombinedImageSampler(1,
        m_Atlas.GetView(), m_Atlas.GetSampler(),
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    }

    m_ShadowData.atlasSize = glm::vec4(
      float(SHADOW_ATLAS_SIZE), float(SHADOW_ATLAS_SIZE),
      1.0f / float(SHADOW_ATLAS_SIZE), 1.0f / float(SHADOW_ATLAS_SIZE));
    m_ShadowData.shadowsEnabled = 0;
    m_ShadowData.cascadeCount = CSM_CASCADE_COUNT;
    m_ShadowData.spotShadowCount = 0;
    m_ShadowData.pointShadowCount = 0;

    for (uint32_t i = 0; i < CSM_CASCADE_COUNT; i++)
    {
      auto sv = ShadowAtlas::GetCascadeViewport(i);
      m_ShadowData.cascades[i].atlasViewport = sv.atlasUV;
    }

    YA_LOG_INFO("Render", "ShadowManager initialized (%d cascades, %dx%d tiles)",
      CSM_CASCADE_COUNT, CASCADE_TILE_SIZE, CASCADE_TILE_SIZE);
  }

  void ShadowManager::Destroy(const RenderContext& ctx)
  {
    for (auto& set : m_LightingShadowDescriptorSets) set.Destroy();
    for (auto& ubo : m_LightingShadowUBOs) ubo.Destroy(ctx);
    m_LightingShadowDescriptorSets.clear();
    m_LightingShadowUBOs.clear();

    m_Atlas.Destroy(ctx);
  }

  void ShadowManager::ComputeCascadeSplits(float nearPlane, float shadowDistance)
  {
    float range = shadowDistance - nearPlane;
    float ratio = shadowDistance / nearPlane;

    m_CascadeSplits[0] = nearPlane;
    for (uint32_t i = 1; i <= CSM_CASCADE_COUNT; i++)
    {
      float p = float(i) / float(CSM_CASCADE_COUNT);
      float logSplit = nearPlane * std::pow(ratio, p);
      float uniformSplit = nearPlane + range * p;
      m_CascadeSplits[i] = SPLIT_LAMBDA * logSplit + (1.0f - SPLIT_LAMBDA) * uniformSplit;
    }
  }

  void ShadowManager::ComputeFrustumSliceSphere(
    const glm::mat4& invView,
    float fov, float aspect,
    float nearDist, float farDist,
    glm::vec3& outCenter, float& outRadius)
  {
    // Slice corners straight from the camera geometry in view space: corners 0-3 sit on the
    // near split plane, 4-7 on the far split plane, looking down -Z.
    float tanHalfFov = std::tan(fov * 0.5f);
    glm::vec3 worldCorners[8];
    for (uint32_t i = 0; i < 8; i++)
    {
      float dist = (i < 4) ? nearDist : farDist;
      float halfH = dist * tanHalfFov;
      float halfW = halfH * aspect;
      float sx = (i & 1) ? 1.0f : -1.0f;
      float sy = (i & 2) ? 1.0f : -1.0f;
      worldCorners[i] = glm::vec3(invView * glm::vec4(sx * halfW, sy * halfH, -dist, 1.0f));
    }

    glm::vec3 center(0.0f);
    for (uint32_t i = 0; i < 8; i++)
      center += worldCorners[i];
    center /= 8.0f;

    float radius = 0.0f;
    for (uint32_t i = 0; i < 8; i++)
    {
      float dist = glm::length(worldCorners[i] - center);
      radius = std::max(radius, dist);
    }

    outCenter = center;
    outRadius = radius;
  }

  float ShadowManager::FitCascadeToFrustum(
    uint32_t cascadeIndex,
    const glm::mat4& invView,
    float fov, float aspect,
    float nearDist, float farDist,
    const glm::vec3& lightDir)
  {
    glm::vec3 center;
    float radius;
    ComputeFrustumSliceSphere(invView, fov, aspect, nearDist, farDist, center, radius);

    return FitCascadeToSphere(cascadeIndex, center, radius, lightDir);
  }

  float ShadowManager::FitCascadeToSphere(
    uint32_t cascadeIndex,
    const glm::vec3& center, float radius,
    const glm::vec3& lightDir)
  {
    // Round up radius to reduce shimmer
    radius = std::ceil(radius * 16.0f) / 16.0f;

    glm::vec3 up = std::abs(glm::dot(lightDir, glm::vec3(0, 1, 0))) > 0.99f
      ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    glm::mat4 lightView = glm::lookAt(center - lightDir * radius, center, up);

    glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius, 0.0f, 2.0f * radius);
    lightProj[1][1] *= -1.0f; // Vulkan Y flip

    glm::mat4 lightVP = lightProj * lightView;

    // Texel snapping: stabilize shadow map when camera moves
    glm::vec4 shadowOrigin = lightVP * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    shadowOrigin *= float(CASCADE_TILE_SIZE) / 2.0f;

    glm::vec4 roundedOrigin = glm::round(shadowOrigin);
    glm::vec4 roundOffset = roundedOrigin - shadowOrigin;
    roundOffset *= 2.0f / float(CASCADE_TILE_SIZE);
    roundOffset.z = 0.0f;
    roundOffset.w = 0.0f;

    lightProj[3] += roundOffset;
    lightVP = lightProj * lightView;

    m_ShadowData.cascades[cascadeIndex].viewProj = lightVP;

    return 2.0f * radius / float(CASCADE_TILE_SIZE);
  }

  void ShadowManager::ComputeCascades(
    const glm::mat4& cameraView,
    float cameraFov, float cameraAspect,
    float cameraNear, float cameraFar,
    float shadowDistance,
    const glm::vec3& lightDirection)
  {
    std::fill(std::begin(b_CascadeRefitThisFit), std::end(b_CascadeRefitThisFit), false);
    m_RefitReasonThisFit = ShadowInvalidation::None;

    if (shadowDistance <= cameraNear)
    {
      m_ShadowData.shadowsEnabled = 0;
      return;
    }

    if (shadowDistance > cameraFar)
    {
      shadowDistance = cameraFar;
    }

    m_ShadowData.shadowsEnabled = 1;

    // Split depths are camera-relative selection depths, not part of any fit,
    // so they keep updating every frame even while every cascade stays frozen.
    ComputeCascadeSplits(cameraNear, shadowDistance);

    glm::mat4 invView = glm::inverse(cameraView);
    glm::vec3 sunDir = glm::normalize(lightDirection);

    // Full-refit triggers shared by all cascades, compared against the inputs
    // the frozen matrices were built from.
    ShadowInvalidation fullReason = ShadowInvalidation::None;
    if (b_FrozenParamsValid)
    {
      if (shadowDistance != m_FrozenShadowDistance
        || cameraFov != m_FrozenFov
        || cameraAspect != m_FrozenAspect
        || cameraNear != m_FrozenNear)
        fullReason = ShadowInvalidation::ShadowParamsChanged;
      else if (sunDir != m_FrozenSunDir
        && glm::dot(sunDir, m_FrozenSunDir) < std::cos(glm::radians(SUN_THRESHOLD_DEG)))
        fullReason = ShadowInvalidation::SunMoved;
    }

    if (!b_FrozenParamsValid || fullReason != ShadowInvalidation::None)
    {
      // ShadowParamsChanged keeps the frozen direction: the sun is still inside
      // the threshold, and fitting against the frozen direction is what keeps
      // the new matrices reproducible until it actually escapes.
      if (fullReason != ShadowInvalidation::ShadowParamsChanged)
        m_FrozenSunDir = sunDir;
      m_FrozenShadowDistance = shadowDistance;
      m_FrozenFov = cameraFov;
      m_FrozenAspect = cameraAspect;
      m_FrozenNear = cameraNear;
      b_FrozenParamsValid = true;
      for (auto& frozen : m_FrozenFits)
        frozen.valid = false;
    }

    // Refit scheduler. While a required sphere still fits inside its frozen one,
    // refitting early is visually safe: the frozen matrix is still valid, the fit
    // just gets re-centered ahead of time. Spreading those proactive refits across
    // frames (budgeted, highest urgency first) keeps several cascades from escaping
    // in the same frame and spiking the pass. The full-refit triggers above are
    // deliberately NOT staggered: all affected cascades refit in the same frame,
    // because cascades fitted against different sun directions would be lit
    // inconsistently.
    glm::vec3 requiredCenters[CSM_CASCADE_COUNT];
    float requiredRadii[CSM_CASCADE_COUNT];
    float urgency[CSM_CASCADE_COUNT] {};
    bool refit[CSM_CASCADE_COUNT];
    int hardRefits = 0;

    for (uint32_t i = 0; i < CSM_CASCADE_COUNT; i++)
    {
      ComputeFrustumSliceSphere(invView, cameraFov, cameraAspect,
        m_CascadeSplits[i], m_CascadeSplits[i + 1], requiredCenters[i], requiredRadii[i]);

      const auto& frozen = m_FrozenFits[i];
      if (!frozen.valid)
      {
        // Cold start or a full-refit trigger: refits now, urgency is set by
        // the fit below once there is a frozen sphere to measure against.
        refit[i] = true;
        hardRefits++;
        continue;
      }

      float required = glm::length(requiredCenters[i] - frozen.center) + requiredRadii[i];
      urgency[i] = required / frozen.radius;

      // Hard escape is the containment comparison itself; it equals urgency >= 1
      // up to the rounding of the division above. Correctness beats smoothness: a
      // hard escape refits immediately and never waits for a budget slot. With
      // proactive scheduling it should almost never happen.
      refit[i] = required > frozen.radius;
      if (refit[i])
        hardRefits++;
    }

    // Right after a refit urgency sits at ~1/margin: the fresh frozen sphere
    // is the required one inflated by the margin. A soft threshold at or
    // below that would re-enqueue the cascade immediately and refit it every
    // frame, so the effective threshold is clamped above 1/margin.
    constexpr float EFFECTIVE_THRESHOLD =
      REFIT_SOFT_THRESHOLD > 1.0f / FIT_MARGIN + 0.02f
        ? REFIT_SOFT_THRESHOLD : 1.0f / FIT_MARGIN + 0.02f;

    // Hard refits happen regardless of the budget but still consume it, so a
    // frame that already paid for an escape adds no scheduled work on top.
    int scheduledSlots = std::max(0, REFIT_BUDGET - hardRefits);
    while (scheduledSlots > 0)
    {
      // Highest urgency first; strict > keeps ties deterministic on the
      // lowest cascade index.
      int best = -1;
      for (uint32_t i = 0; i < CSM_CASCADE_COUNT; i++)
      {
        if (refit[i] || !m_FrozenFits[i].valid) continue;
        if (urgency[i] < EFFECTIVE_THRESHOLD) continue;
        if (best < 0 || urgency[i] > urgency[best]) best = int(i);
      }
      if (best < 0) break;
      refit[best] = true;
      scheduledSlots--;
    }

    for (uint32_t i = 0; i < CSM_CASCADE_COUNT; i++)
    {
      auto& frozen = m_FrozenFits[i];

      float texelWorldSize;
      if (!refit[i])
      {
        // Verbatim reuse is the entire point: the future tile cache keys on the
        // matrix staying bit-identical between refits.
        m_ShadowData.cascades[i].viewProj = frozen.viewProj;
        texelWorldSize = frozen.texelWorldSize;
      }
      else
      {
        // The margin is what buys frames of reuse: the camera can move until the
        // required sphere escapes the inflated one. Pre-rounded the same way
        // FitCascadeToSphere rounds (idempotent), so the stored radius is the
        // fitted one and the containment test cannot flicker at the boundary.
        float inflated = std::ceil(requiredRadii[i] * FIT_MARGIN * 16.0f) / 16.0f;
        texelWorldSize = FitCascadeToSphere(i, requiredCenters[i], inflated, m_FrozenSunDir);

        frozen = {
          .viewProj = m_ShadowData.cascades[i].viewProj,
          .center = requiredCenters[i],
          .radius = inflated,
          .texelWorldSize = texelWorldSize,
          .valid = true,
        };

        // A scheduled refit IS a refit: it records CascadeRefit like an organic
        // escape, so the partial rebuild picks its tile up unchanged.
        b_CascadeRefitThisFit[i] = true;
        m_RefitReasonThisFit = fullReason != ShadowInvalidation::None
          ? fullReason : ShadowInvalidation::CascadeRefit;
      }

      // Normal bias = 1.5 texels in world space, scales automatically per
      // cascade; while frozen it comes from the frozen fit's texel size.
      m_ShadowData.cascades[i].splitDepthAndBias = glm::vec4(
        m_CascadeSplits[i + 1],
        0.0f,
        texelWorldSize * 1.5f,
        0.0f);
    }
  }

  void ShadowManager::ComputeCascadesAroundPoint(
    const glm::vec3& center,
    float nearPlane,
    float shadowDistance,
    const glm::vec3& lightDirection,
    float volumeRadius)
  {
    if (shadowDistance <= nearPlane)
    {
      m_ShadowData.shadowsEnabled = 0;
      return;
    }

    m_ShadowData.shadowsEnabled = 1;

    ComputeCascadeSplits(nearPlane, shadowDistance);

    glm::vec3 lightDir = glm::normalize(lightDirection);

    for (uint32_t i = 0; i < CSM_CASCADE_COUNT; i++)
    {
      // Cascades are picked in the shader by view depth, but a 90 degree cube face
      // reaches sqrt(3) times its view depth at the frustum corners. Inflating the
      // sphere by that factor keeps every texel a cascade can be selected for inside
      // the map it was fitted to.
      // Capture points spread over a box reach volumeRadius further out than the
      // center does, so the sphere grows by that much and one atlas serves them all.
      float radius = m_CascadeSplits[i + 1] * OMNI_CASCADE_SLACK + volumeRadius;

      float texelWorldSize = FitCascadeToSphere(i, center, radius, lightDir);

      m_ShadowData.cascades[i].splitDepthAndBias = glm::vec4(
        m_CascadeSplits[i + 1],
        0.0f,
        texelWorldSize * 1.5f,
        0.0f);
    }
  }

  void ShadowManager::ComputeSpotShadow(
    uint32_t spotIndex,
    const glm::vec3& position,
    const glm::vec3& direction,
    float outerCone, float radius)
  {
    auto sv = ShadowAtlas::GetSpotViewport(spotIndex);

    glm::vec3 up = std::abs(glm::dot(direction, glm::vec3(0, 1, 0))) > 0.99f
      ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    glm::mat4 view = glm::lookAt(position, position + direction, up);

    float fov = outerCone * 2.0f;
    glm::mat4 proj = glm::perspective(fov, 1.0f, SHADOW_NEAR_PLANE, radius);
    proj[1][1] *= -1.0f;

    glm::mat4 viewProj = proj * view;

    auto& spot = m_ShadowData.spotShadows[spotIndex];
    spot.viewProj = viewProj;
    spot.atlasViewport = sv.atlasUV;

    float texelWorldSize = 2.0f * std::tan(outerCone) * radius / float(SHADOW_SPOT_SIZE);
    spot.biasData = glm::vec4(0.0f, texelWorldSize * 1.5f, 0.0f, 0.0f);
  }

  void ShadowManager::ComputePointShadow(
    uint32_t pointIndex,
    const glm::vec3& position, float radius)
  {
    static const glm::vec3 faceDirections[6] = {
      {  1,  0,  0 }, { -1,  0,  0 },
      {  0,  1,  0 }, {  0, -1,  0 },
      {  0,  0,  1 }, {  0,  0, -1 },
    };
    static const glm::vec3 faceUps[6] = {
      { 0, -1,  0 }, { 0, -1,  0 },
      { 0,  0,  1 }, { 0,  0, -1 },
      { 0, -1,  0 }, { 0, -1,  0 },
    };

    // Slightly wider than 90 degrees to create overlap at cube face seams
    glm::mat4 proj = glm::perspective(glm::radians(95.0f), 1.0f, SHADOW_NEAR_PLANE, radius);
    proj[1][1] *= -1.0f;

    auto& point = m_ShadowData.pointShadows[pointIndex];
    point.positionFarPlane = glm::vec4(position, radius);

    float texelWorldSize = 2.0f * radius / float(SHADOW_POINT_FACE_SIZE);
    point.biasData = glm::vec4(0.0f, texelWorldSize * 1.5f, 0.0f, 0.0f);

    for (uint32_t f = 0; f < 6; f++)
    {
      glm::mat4 view = glm::lookAt(position, position + faceDirections[f], faceUps[f]);
      point.faceViewProj[f] = proj * view;

      auto sv = ShadowAtlas::GetPointFaceViewport(pointIndex, f);
      point.faceAtlasViewport[f] = sv.atlasUV;
    }
  }

  void ShadowManager::SetUp(uint32_t frameIndex)
  {
    m_LightingShadowUBOs[frameIndex].Update(m_ShadowData);
  }
}
