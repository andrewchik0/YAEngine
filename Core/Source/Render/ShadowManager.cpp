#include "ShadowManager.h"
#include "RenderContext.h"
#include "Utils/Log.h"

namespace YAEngine
{
  void ShadowManager::Init(const RenderContext& ctx)
  {
    m_Atlas.Init(ctx);

    uint32_t framesInFlight = ctx.maxFramesInFlight;

    // Shadow cascade UBO (used in shadow vertex shader at set 0)
    m_CascadeDescriptorSets.resize(framesInFlight);
    m_CascadeUBOs.resize(framesInFlight);

    VkDescriptorSetLayout cascadeLayout = nullptr;
    for (uint32_t i = 0; i < framesInFlight; i++)
    {
      SetDescription desc = {
        .set = 0,
        .bindings = {
          { { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT } }
        }
      };
      if (i == 0)
      {
        m_CascadeDescriptorSets[i].Init(ctx, desc);
        cascadeLayout = m_CascadeDescriptorSets[i].GetLayout();
      }
      else
      {
        m_CascadeDescriptorSets[i].Init(ctx, cascadeLayout);
      }
      m_CascadeUBOs[i].Create(ctx, sizeof(ShadowBuffer));
      m_CascadeDescriptorSets[i].WriteUniformBuffer(0, m_CascadeUBOs[i].Get(), sizeof(ShadowBuffer));
    }

    // Lighting pass shadow descriptor sets (UBO + sampler, per-frame)
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

    // Initialize ShadowBuffer defaults
    m_ShadowData.atlasSize = glm::vec4(
      float(SHADOW_ATLAS_SIZE), float(SHADOW_ATLAS_SIZE),
      1.0f / float(SHADOW_ATLAS_SIZE), 1.0f / float(SHADOW_ATLAS_SIZE));
    m_ShadowData.shadowsEnabled = 0;
    m_ShadowData.cascadeCount = CSM_CASCADE_COUNT;
    m_ShadowData.spotShadowCount = 0;
    m_ShadowData.pointShadowCount = 0;

    // Set atlas viewport UVs for each cascade (from ShadowAtlas layout)
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
    for (auto& set : m_CascadeDescriptorSets) set.Destroy();
    for (auto& ubo : m_CascadeUBOs) ubo.Destroy(ctx);
    m_CascadeDescriptorSets.clear();
    m_CascadeUBOs.clear();

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

  float ShadowManager::FitCascadeToFrustum(
    uint32_t cascadeIndex,
    const glm::mat4& invView,
    float fov, float aspect,
    float nearDist, float farDist,
    const glm::vec3& lightDir)
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

    // Compute bounding sphere center
    glm::vec3 center(0.0f);
    for (uint32_t i = 0; i < 8; i++)
      center += worldCorners[i];
    center /= 8.0f;

    // Compute bounding sphere radius
    float radius = 0.0f;
    for (uint32_t i = 0; i < 8; i++)
    {
      float dist = glm::length(worldCorners[i] - center);
      radius = std::max(radius, dist);
    }

    return FitCascadeToSphere(cascadeIndex, center, radius, lightDir);
  }

  float ShadowManager::FitCascadeToSphere(
    uint32_t cascadeIndex,
    const glm::vec3& center, float radius,
    const glm::vec3& lightDir)
  {
    // Round up radius to reduce shimmer
    radius = std::ceil(radius * 16.0f) / 16.0f;

    // Light view matrix
    glm::vec3 up = std::abs(glm::dot(lightDir, glm::vec3(0, 1, 0))) > 0.99f
      ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    glm::mat4 lightView = glm::lookAt(center - lightDir * radius, center, up);

    // Orthographic projection
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

    ComputeCascadeSplits(cameraNear, shadowDistance);

    glm::mat4 invView = glm::inverse(cameraView);

    for (uint32_t i = 0; i < CSM_CASCADE_COUNT; i++)
    {
      float texelWorldSize = FitCascadeToFrustum(i, invView, cameraFov, cameraAspect,
        m_CascadeSplits[i], m_CascadeSplits[i + 1], glm::normalize(lightDirection));

      // Normal bias = 1.5 texels in world space, scales automatically per cascade
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
    m_CascadeUBOs[frameIndex].Update(m_ShadowData);
    m_LightingShadowUBOs[frameIndex].Update(m_ShadowData);
  }
}
