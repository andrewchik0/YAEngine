#include "Render.h"

#include "DebugMarker.h"
#include "VulkanVertexBuffer.h"
#include "Assets/AssetManager.h"
#include "RenderObject.h"
#include "FrustumCull.h"
#include "Scene/Components.h"

#include "Utils/Utils.h"

namespace YAEngine
{
  void Render::DrawMeshes(VkCommandBuffer cmd, uint32_t frameIndex, FrameContext& frame,
    VkDescriptorSet frameUBOOverride)
  {
    auto currentFrame = frameIndex;
    VkDescriptorSet frameUBO = frameUBOOverride != VK_NULL_HANDLE
      ? frameUBOOverride : m_FrameUniformBuffer.GetDescriptorSet(currentFrame);
    auto& meshManager = frame.assets.Meshes();
    auto& materialManager = frame.assets.Materials();
    auto& cubeMapManager = frame.assets.CubeMaps();
    auto skybox = frame.snapshot.skybox;

    m_DrawCommands.clear();

    uint32_t visibleCount = frame.snapshot.visibleCount;
    m_DrawCommands.reserve(visibleCount);
    for (uint32_t i = 0; i < visibleCount; i++)
    {
      auto& obj = frame.snapshot.objects[i];
      bool isInstanced = (obj.instanceData != nullptr) && !obj.noShading;

      m_DrawCommands.push_back({
        .instanced = isInstanced,
        .doubleSided = obj.doubleSided,
        .noShading = obj.noShading,
        .isTerrain = obj.isTerrain,
        .isAlphaTest = obj.isAlphaTest,
        .materialIndex = obj.material.index,
        .materialGeneration = obj.material.generation,
        .meshIndex = obj.mesh.index,
        .meshGeneration = obj.mesh.generation,
        .worldTransform = obj.worldTransform,
        .instanceData = obj.instanceData,
        .instanceOffset = obj.instanceOffset,
      });
    }

    std::sort(m_DrawCommands.begin(), m_DrawCommands.end(),
      [](const DrawCommand& a, const DrawCommand& b)
      {
        uint8_t ka = a.SortKey(), kb = b.SortKey();
        if (ka != kb) return ka < kb;
        if (a.materialIndex != b.materialIndex) return a.materialIndex < b.materialIndex;
        return a.meshIndex < b.meshIndex;
      });

    uint32_t lastMaterialIndex = UINT32_MAX;
    uint32_t lastMaterialGeneration = UINT32_MAX;
    for (auto& dc : m_DrawCommands)
    {
      if (dc.isTerrain) continue;
      if (dc.materialIndex == lastMaterialIndex && dc.materialGeneration == lastMaterialGeneration) continue;
      lastMaterialIndex = dc.materialIndex;
      lastMaterialGeneration = dc.materialGeneration;

      MaterialHandle matHandle { dc.materialIndex, dc.materialGeneration };
      auto& mat = materialManager.Get(matHandle);
      mat.cubemap = skybox;
      materialManager.GetVulkanMaterial(matHandle).Bind(frame.assets.Textures(), cubeMapManager, frame.cubicResources, mat, currentFrame, m_NoneTexture);
    }

    // Pre-bind terrain material
    if (frame.snapshot.terrainData.layer1 != nullptr &&
        materialManager.Has(frame.snapshot.terrainData.layer0))
    {
      auto& layer0 = materialManager.Get(frame.snapshot.terrainData.layer0);
      m_TerrainMaterial.Bind(frame.assets.Textures(), layer0,
        *frame.snapshot.terrainData.layer1, frame.snapshot.terrainData.roadPolyline,
        currentFrame, m_NoneTexture);
    }

    uint8_t lastSortKey = UINT8_MAX;
    lastMaterialIndex = UINT32_MAX;
    uint32_t lastMaterialGen = UINT32_MAX;
    VulkanPipeline* currentPipeline = nullptr;

    for (auto& dc : m_DrawCommands)
    {
      MaterialHandle matHandle { dc.materialIndex, dc.materialGeneration };
      MeshHandle meshHandle { dc.meshIndex, dc.meshGeneration };

      uint8_t sortKey = dc.SortKey();
      if (sortKey != lastSortKey)
      {
        currentPipeline = &GetForwardPipeline(dc);
        currentPipeline->Bind(cmd);
        currentPipeline->BindDescriptorSets(cmd, {frameUBO}, 0);
        lastSortKey = sortKey;
        lastMaterialIndex = UINT32_MAX;
        lastMaterialGen = UINT32_MAX;

        if (sortKey == 5)
        {
          currentPipeline->BindDescriptorSets(cmd, {m_TerrainMaterial.GetDescriptorSet(currentFrame)}, 1);
        }
      }

      if (sortKey != 5 && (dc.materialIndex != lastMaterialIndex || dc.materialGeneration != lastMaterialGen))
      {
        currentPipeline->BindDescriptorSets(cmd, {materialManager.GetVulkanMaterial(matHandle).GetDescriptorSet(currentFrame)}, 1);
        lastMaterialIndex = dc.materialIndex;
        lastMaterialGen = dc.materialGeneration;
      }

      struct
      {
        glm::mat4 model;
        int offset = 0;
      } data;
      data.model = dc.worldTransform;
      if (dc.instanced)
        data.offset = dc.instanceOffset / sizeof(glm::mat4);
      currentPipeline->PushConstants(cmd, &data);

      uint32_t instanceCount = 1;
      if (dc.instanced)
      {
        instanceCount = uint32_t(dc.instanceData->size());
        currentPipeline->BindDescriptorSets(cmd, { m_InstanceDescriptorSet.Get() }, 2);
        m_InstanceBuffer.Update(dc.instanceOffset, dc.instanceData->data(), uint32_t(instanceCount * sizeof(glm::mat4)));
      }

      auto& vb = meshManager.GetVertexBuffer(meshHandle);
      m_Stats.drawCalls++;
      m_Stats.triangles += uint32_t(vb.GetIndexCount() / 3) * instanceCount;
      m_Stats.vertices += uint32_t(vb.GetIndexCount()) * instanceCount;
      vb.Draw(cmd, instanceCount);
    }

  }

  void Render::SetUpCamera(FrameContext& frame)
  {
    auto& cam = frame.snapshot.camera;

    glm::mat4 world =
      glm::translate(glm::mat4(1.0f), cam.position) *
      glm::mat4_cast(cam.rotation);

    glm::mat4 view = glm::inverse(world);

    glm::mat4 proj = glm::perspective(
      cam.fov,
      cam.aspectRatio,
      cam.nearPlane,
      cam.farPlane
    );

    proj[1][1] *= -1.0f;

    // Store previous frame matrices (unjittered) for reprojection
    m_FrameUniformBuffer.uniforms.prevView = m_PrevView;
    m_FrameUniformBuffer.uniforms.prevProj = m_PrevProj;
    m_PrevView = view;
    m_PrevProj = proj;

    if (b_TAAEnabled)
    {
      glm::vec2 jitter = GetTAAJitter(m_GlobalFrameIndex);

#ifdef YA_EDITOR
      jitter.x /= float(m_ViewportWidth);
      jitter.y /= float(m_ViewportHeight);
#else
      jitter.x /= float(frame.windowWidth);
      jitter.y /= float(frame.windowHeight);
#endif

      m_FrameUniformBuffer.uniforms.jitterX = jitter.x;
      m_FrameUniformBuffer.uniforms.jitterY = jitter.y;

      proj[2][0] += jitter.x;
      proj[2][1] += jitter.y;
    }
    else
    {
      m_FrameUniformBuffer.uniforms.jitterX = 0.0f;
      m_FrameUniformBuffer.uniforms.jitterY = 0.0f;
    }

    m_FrameUniformBuffer.uniforms.view = view;
    m_FrameUniformBuffer.uniforms.proj = proj;
    m_FrameUniformBuffer.uniforms.invProj = glm::inverse(proj);
    m_FrameUniformBuffer.uniforms.invView = glm::inverse(view);
    m_FrameUniformBuffer.uniforms.nearPlane = cam.nearPlane;
    m_FrameUniformBuffer.uniforms.farPlane = cam.farPlane;
    m_FrameUniformBuffer.uniforms.cameraPosition = cam.position;
    m_FrameUniformBuffer.uniforms.cameraDirection = glm::normalize(-glm::vec3(world[2]));
    m_FrameUniformBuffer.uniforms.fov = cam.fov;
  }

  void Render::DrawMeshesDepthOnly(VkCommandBuffer cmd, uint32_t frameIndex, FrameContext& frame,
    VkDescriptorSet frameUBOOverride)
  {
    auto currentFrame = frameIndex;
    VkDescriptorSet frameUBO = frameUBOOverride != VK_NULL_HANDLE
      ? frameUBOOverride : m_FrameUniformBuffer.GetDescriptorSet(currentFrame);
    auto& meshManager = frame.assets.Meshes();
    auto& materialManager = frame.assets.Materials();
    auto& cubeMapManager = frame.assets.CubeMaps();
    auto skybox = frame.snapshot.skybox;

    m_DepthDrawCommands.clear();
    m_DepthDrawCommands.reserve(frame.snapshot.visibleCount);
    for (uint32_t i = 0; i < frame.snapshot.visibleCount; i++)
    {
      auto& obj = frame.snapshot.objects[i];
      if (obj.noShading) continue;

      m_DepthDrawCommands.push_back({
        .instanced = (obj.instanceData != nullptr),
        .doubleSided = obj.doubleSided,
        .noShading = false,
        .isAlphaTest = obj.isAlphaTest,
        .materialIndex = obj.material.index,
        .materialGeneration = obj.material.generation,
        .meshIndex = obj.mesh.index,
        .meshGeneration = obj.mesh.generation,
        .worldTransform = obj.worldTransform,
        .instanceData = obj.instanceData,
        .instanceOffset = obj.instanceOffset,
      });
    }

    std::sort(m_DepthDrawCommands.begin(), m_DepthDrawCommands.end(),
      [](const DrawCommand& a, const DrawCommand& b)
      {
        uint8_t ka = a.SortKey(), kb = b.SortKey();
        if (ka != kb) return ka < kb;
        if (a.materialIndex != b.materialIndex) return a.materialIndex < b.materialIndex;
        return a.meshIndex < b.meshIndex;
      });

    // Pre-bind materials needed by alpha-test depth pipelines
    uint32_t lastMaterialIndex = UINT32_MAX;
    uint32_t lastMaterialGeneration = UINT32_MAX;
    for (auto& dc : m_DepthDrawCommands)
    {
      if (!dc.isAlphaTest) continue;
      if (dc.materialIndex == lastMaterialIndex && dc.materialGeneration == lastMaterialGeneration) continue;
      lastMaterialIndex = dc.materialIndex;
      lastMaterialGeneration = dc.materialGeneration;

      MaterialHandle matHandle { dc.materialIndex, dc.materialGeneration };
      auto& mat = materialManager.Get(matHandle);
      mat.cubemap = skybox;
      materialManager.GetVulkanMaterial(matHandle).Bind(frame.assets.Textures(), cubeMapManager, frame.cubicResources, mat, currentFrame, m_NoneTexture);
    }

    uint8_t lastSortKey = UINT8_MAX;
    lastMaterialIndex = UINT32_MAX;
    uint32_t lastMaterialGen = UINT32_MAX;
    VulkanPipeline* currentPipeline = nullptr;

    for (auto& dc : m_DepthDrawCommands)
    {
      MeshHandle meshHandle { dc.meshIndex, dc.meshGeneration };

      uint8_t sortKey = dc.SortKey();
      if (sortKey != lastSortKey)
      {
        currentPipeline = &GetDepthPipeline(dc);
        currentPipeline->Bind(cmd);
        currentPipeline->BindDescriptorSets(cmd, {frameUBO}, 0);
        lastSortKey = sortKey;
        lastMaterialIndex = UINT32_MAX;
        lastMaterialGen = UINT32_MAX;
      }

      if (dc.isAlphaTest && (dc.materialIndex != lastMaterialIndex || dc.materialGeneration != lastMaterialGen))
      {
        MaterialHandle matHandle { dc.materialIndex, dc.materialGeneration };
        currentPipeline->BindDescriptorSets(cmd, {materialManager.GetVulkanMaterial(matHandle).GetDescriptorSet(currentFrame)}, 1);
        lastMaterialIndex = dc.materialIndex;
        lastMaterialGen = dc.materialGeneration;
      }

      struct
      {
        glm::mat4 model;
        int offset = 0;
      } data;
      data.model = dc.worldTransform;
      data.offset = dc.instanceOffset / sizeof(glm::mat4);
      currentPipeline->PushConstants(cmd, &data);

      uint32_t instanceCount = 1;
      if (dc.instanced)
      {
        instanceCount = uint32_t(dc.instanceData->size());
        uint32_t instanceSetIndex = dc.isAlphaTest ? 2 : 1;
        currentPipeline->BindDescriptorSets(cmd, { m_InstanceDescriptorSet.Get() }, instanceSetIndex);
        m_InstanceBuffer.Update(dc.instanceOffset, dc.instanceData->data(), uint32_t(instanceCount * sizeof(glm::mat4)));
      }

      auto& vb = meshManager.GetVertexBuffer(meshHandle);
      if (dc.isAlphaTest)
        vb.Draw(cmd, instanceCount);
      else
        vb.DrawPositionOnly(cmd, instanceCount);
    }
  }

  void Render::RenderShadowMaps(FrameContext& frame)
  {
    // Collect shadow draw commands from ALL objects (not just camera-visible).
    // Objects outside the camera frustum can still cast shadows into the view.
    m_ShadowDrawCommands.clear();
    uint32_t totalCount = uint32_t(frame.snapshot.objects.size());
    m_ShadowDrawCommands.reserve(totalCount);
    for (uint32_t i = 0; i < totalCount; i++)
    {
      auto& obj = frame.snapshot.objects[i];
      if (obj.noShading) continue;

      m_ShadowDrawCommands.push_back({
        .instanced = (obj.instanceData != nullptr),
        .doubleSided = obj.doubleSided,
        .noShading = false,
        .isAlphaTest = obj.isAlphaTest,
        .materialIndex = obj.material.index,
        .materialGeneration = obj.material.generation,
        .meshIndex = obj.mesh.index,
        .meshGeneration = obj.mesh.generation,
        .worldTransform = obj.worldTransform,
        .boundsMin = obj.boundsMin,
        .boundsMax = obj.boundsMax,
        .instanceData = obj.instanceData,
        .instanceOffset = obj.instanceOffset,
      });
    }

    std::sort(m_ShadowDrawCommands.begin(), m_ShadowDrawCommands.end(),
      [](const DrawCommand& a, const DrawCommand& b)
      {
        uint8_t ka = a.SortKey(), kb = b.SortKey();
        if (ka != kb) return ka < kb;
        if (a.materialIndex != b.materialIndex) return a.materialIndex < b.materialIndex;
        return a.meshIndex < b.meshIndex;
      });

    // Pre-bind materials needed by alpha-test shadow pipelines
    auto& materialManager = frame.assets.Materials();
    auto& cubeMapManager = frame.assets.CubeMaps();
    auto skybox = frame.snapshot.skybox;
    uint32_t preLastMaterialIndex = UINT32_MAX;
    uint32_t preLastMaterialGeneration = UINT32_MAX;
    for (auto& dc : m_ShadowDrawCommands)
    {
      if (!dc.isAlphaTest) continue;
      if (dc.materialIndex == preLastMaterialIndex && dc.materialGeneration == preLastMaterialGeneration) continue;
      preLastMaterialIndex = dc.materialIndex;
      preLastMaterialGeneration = dc.materialGeneration;

      MaterialHandle matHandle { dc.materialIndex, dc.materialGeneration };
      auto& mat = materialManager.Get(matHandle);
      mat.cubemap = skybox;
      materialManager.GetVulkanMaterial(matHandle).Bind(frame.assets.Textures(), cubeMapManager, frame.cubicResources, mat, m_Backend.GetCurrentFrameIndex(), m_NoneTexture);
    }

    bool hasDirectionalShadow = b_ShadowsEnabled && frame.snapshot.directionalShadow.castShadow;
    bool hasSpotShadows = b_ShadowsEnabled && !frame.snapshot.spotShadowRequests.empty();
    bool hasPointShadows = b_ShadowsEnabled && !frame.snapshot.pointShadowRequests.empty();

    if (!hasDirectionalShadow && !hasSpotShadows && !hasPointShadows)
    {
      m_ShadowManager.SetEnabled(false);
      m_ShadowManager.SetSpotShadowCount(0);
      m_ShadowManager.SetPointShadowCount(0);
      m_ShadowManager.SetUp(m_Backend.GetCurrentFrameIndex());
      return;
    }

    // Compute shadow matrices
    if (hasDirectionalShadow)
    {
      auto& cam = frame.snapshot.camera;
      auto& shadow = frame.snapshot.directionalShadow;
      m_ShadowManager.SetEnabled(true);
      m_ShadowManager.ComputeCascades(
        m_FrameUniformBuffer.uniforms.view,
        m_PrevProj,
        cam.nearPlane, cam.farPlane,
        shadow.shadowDistance,
        shadow.direction);
    }
    else
    {
      m_ShadowManager.SetEnabled(false);
    }

    m_ShadowManager.SetSpotShadowCount(int(frame.snapshot.spotShadowRequests.size()));
    for (uint32_t i = 0; i < frame.snapshot.spotShadowRequests.size(); i++)
    {
      auto& req = frame.snapshot.spotShadowRequests[i];
      m_ShadowManager.ComputeSpotShadow(i, req.position, req.direction, req.outerCone, req.radius);
    }

    m_ShadowManager.SetPointShadowCount(int(frame.snapshot.pointShadowRequests.size()));
    for (uint32_t i = 0; i < frame.snapshot.pointShadowRequests.size(); i++)
    {
      auto& req = frame.snapshot.pointShadowRequests[i];
      m_ShadowManager.ComputePointShadow(i, req.position, req.radius);
    }

    auto currentFrame = m_Backend.GetCurrentFrameIndex();
    m_ShadowManager.SetUp(currentFrame);

    auto cmd = m_Backend.GetCurrentCommandBuffer();
    auto& atlas = m_ShadowManager.GetAtlas();

    // Begin shadow atlas render pass (clears entire atlas)
    VkClearValue clearValue {};
    clearValue.depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rpBegin {};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = atlas.GetRenderPass();
    rpBegin.framebuffer = atlas.GetFramebuffer();
    rpBegin.renderArea = { {0, 0}, atlas.GetExtent() };
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearValue;

    DebugMarker::BeginLabel(cmd, "Shadows");
    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetDepthBias(cmd, 0.5f, 0.0f, 1.5f);

    auto& meshManager = frame.assets.Meshes();

    // Upload instance data once before all shadow passes
    for (auto& dc : m_ShadowDrawCommands)
    {
      if (dc.instanced)
      {
        m_InstanceBuffer.Update(dc.instanceOffset, dc.instanceData->data(),
          uint32_t(dc.instanceData->size() * sizeof(glm::mat4)));
      }
    }

    // Helper lambda: draw all shadow commands with given shadowMatrixIndex
    // frustumPlanes + planeCount for frustum culling; nullptr disables frustum culling
    auto drawShadowPass = [&](int shadowMatrixIndex, const ShadowViewport& sv,
      const FrustumPlane* frustumPlanes = nullptr, int planeCount = 6)
    {
      vkCmdSetViewport(cmd, 0, 1, &sv.viewport);
      vkCmdSetScissor(cmd, 0, 1, &sv.scissor);

      uint8_t lastSortKey = UINT8_MAX;
      uint32_t lastMaterialIndex = UINT32_MAX;
      uint32_t lastMaterialGen = UINT32_MAX;
      VulkanPipeline* currentPipeline = nullptr;

      for (auto& dc : m_ShadowDrawCommands)
      {
        bool hasBounds = dc.boundsMin.x <= dc.boundsMax.x;
        if (hasBounds && frustumPlanes)
        {
          if (!IsAABBVisible(dc.boundsMin, dc.boundsMax, frustumPlanes, planeCount))
            continue;
        }

        MeshHandle meshHandle { dc.meshIndex, dc.meshGeneration };
        uint8_t sortKey = dc.SortKey();

        if (sortKey != lastSortKey)
        {
          currentPipeline = &m_PSOCache.Get(m_ShadowPipelines[sortKey]);
          currentPipeline->Bind(cmd);
          currentPipeline->BindDescriptorSets(cmd,
            { m_ShadowManager.GetShadowCascadeUBODescriptorSet(currentFrame) }, 0);
          lastSortKey = sortKey;
          lastMaterialIndex = UINT32_MAX;
          lastMaterialGen = UINT32_MAX;
        }

        if (dc.isAlphaTest && (dc.materialIndex != lastMaterialIndex || dc.materialGeneration != lastMaterialGen))
        {
          MaterialHandle matHandle { dc.materialIndex, dc.materialGeneration };
          currentPipeline->BindDescriptorSets(cmd,
            { materialManager.GetVulkanMaterial(matHandle).GetDescriptorSet(currentFrame) }, 1);
          lastMaterialIndex = dc.materialIndex;
          lastMaterialGen = dc.materialGeneration;
        }

        struct
        {
          glm::mat4 model;
          int offset = 0;
          int shadowMatrixIndex = 0;
        } data;
        data.model = dc.worldTransform;
        data.offset = dc.instanceOffset / sizeof(glm::mat4);
        data.shadowMatrixIndex = shadowMatrixIndex;
        currentPipeline->PushConstants(cmd, &data);

        uint32_t instanceCount = 1;
        if (dc.instanced)
        {
          instanceCount = uint32_t(dc.instanceData->size());
          uint32_t instanceSetIndex = dc.isAlphaTest ? 2 : 1;
          currentPipeline->BindDescriptorSets(cmd, { m_InstanceDescriptorSet.Get() }, instanceSetIndex);
        }

        auto& vb = meshManager.GetVertexBuffer(meshHandle);
        if (dc.isAlphaTest)
          vb.Draw(cmd, instanceCount);
        else
          vb.DrawPositionOnly(cmd, instanceCount);
      }
    };

    auto& shadowData = m_ShadowManager.GetShadowData();

    // CSM cascades - frustum cull with 5 planes (skip near plane to keep distant shadow casters)
    if (hasDirectionalShadow)
    {
      DebugMarker::BeginLabel(cmd, "CSM");
      for (uint32_t cascade = 0; cascade < CSM_CASCADE_COUNT; cascade++)
      {
        FrustumPlane allPlanes[6];
        ExtractFrustumPlanes(shadowData.cascades[cascade].viewProj, allPlanes);

        // Skip near plane (index 4): left, right, bottom, top, far
        FrustumPlane csmPlanes[5] = { allPlanes[0], allPlanes[1], allPlanes[2], allPlanes[3], allPlanes[5] };

        auto sv = ShadowAtlas::GetCascadeViewport(cascade);
        drawShadowPass(int(cascade), sv, csmPlanes, 5);
      }
      DebugMarker::EndLabel(cmd);
    }

    // Spot shadows - full 6-plane frustum culling
    if (hasSpotShadows)
      DebugMarker::BeginLabel(cmd, "Spot Shadows");
    for (uint32_t i = 0; i < frame.snapshot.spotShadowRequests.size(); i++)
    {
      FrustumPlane spotPlanes[6];
      ExtractFrustumPlanes(shadowData.spotShadows[i].viewProj, spotPlanes);

      auto sv = ShadowAtlas::GetSpotViewport(i);
      drawShadowPass(int(SHADOW_SPOT_MATRIX_OFFSET + i), sv, spotPlanes, 6);
    }
    if (hasSpotShadows)
      DebugMarker::EndLabel(cmd);

    // Point shadows - per-face frustum culling
    if (hasPointShadows)
      DebugMarker::BeginLabel(cmd, "Point Shadows");
    for (uint32_t i = 0; i < frame.snapshot.pointShadowRequests.size(); i++)
    {
      for (uint32_t face = 0; face < 6; face++)
      {
        FrustumPlane facePlanes[6];
        ExtractFrustumPlanes(shadowData.pointShadows[i].faceViewProj[face], facePlanes);

        auto sv = ShadowAtlas::GetPointFaceViewport(i, face);
        drawShadowPass(int(SHADOW_POINT_MATRIX_OFFSET + i * 6 + face), sv, facePlanes, 6);
      }
    }
    if (hasPointShadows)
      DebugMarker::EndLabel(cmd);

    vkCmdEndRenderPass(cmd);
    DebugMarker::EndLabel(cmd);
  }

  void Render::DrawQuad(VkCommandBuffer cmd)
  {
    vkCmdDraw(cmd, 3, 1, 0, 0);
  }
}
