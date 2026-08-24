#include "Render.h"

#include "DebugMarker.h"
#include "GeometryArena.h"
#include "VulkanVertexBuffer.h"
#include "Assets/AssetManager.h"
#include "RenderObject.h"
#include "FrustumCull.h"
#include "Scene/Components.h"

#include "Utils/Log.h"
#include "Utils/Utils.h"
#include "Utils/Projection.h"

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
    m_TransparentDrawCommands.clear();

    uint32_t visibleCount = frame.snapshot.visibleCount;
    m_DrawCommands.reserve(visibleCount);
    glm::vec3 camPos = frame.snapshot.camera.position;
    for (uint32_t i = 0; i < visibleCount; i++)
    {
      auto& obj = frame.snapshot.objects[i];
      bool isInstanced = (obj.instanceData != nullptr) && !obj.noShading;

      if (obj.isTransparent)
      {
        glm::vec3 center = glm::vec3(obj.worldTransform[3]);
        glm::vec3 d = center - camPos;
        m_TransparentDrawCommands.push_back({
          .instanced = isInstanced,
          .doubleSided = obj.doubleSided,
          .noShading = obj.noShading,
          .isTerrain = obj.isTerrain,
          .isAlphaTest = obj.isAlphaTest,
          .isTransparent = true,
          .materialIndex = obj.material.index,
          .materialGeneration = obj.material.generation,
          .meshIndex = obj.mesh.index,
          .meshGeneration = obj.mesh.generation,
          .worldTransform = obj.worldTransform,
          .instanceData = obj.instanceData,
          .instanceOffset = obj.instanceOffset,
          .cameraDistanceSq = glm::dot(d, d),
        });
        continue;
      }

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

    bool wireframeMode = (m_CurrentTexture == DEBUG_VIEW_WIREFRAME);
    if (wireframeMode)
      vkCmdSetDepthBias(cmd, 1.0f, 0.0f, 1.0f);

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
        currentPipeline = wireframeMode ? &GetWireframePipeline(dc) : &GetForwardPipeline(dc);
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

    // Wireframe debug draws transparent geometry in the GBuffer pass to get everything
    // into gbuffer0 in a single pass; the transparent pass is skipped via early return.
    if (wireframeMode && !m_TransparentDrawCommands.empty())
    {
      // Pre-bind transparent materials (texture upload + descriptor write)
      uint32_t preLastMaterialIndex = UINT32_MAX;
      uint32_t preLastMaterialGen = UINT32_MAX;
      for (auto& dc : m_TransparentDrawCommands)
      {
        if (dc.materialIndex == preLastMaterialIndex && dc.materialGeneration == preLastMaterialGen) continue;
        preLastMaterialIndex = dc.materialIndex;
        preLastMaterialGen = dc.materialGeneration;

        MaterialHandle matHandle { dc.materialIndex, dc.materialGeneration };
        auto& mat = materialManager.Get(matHandle);
        mat.cubemap = skybox;
        materialManager.GetVulkanMaterial(matHandle).Bind(frame.assets.Textures(), cubeMapManager, frame.cubicResources, mat, currentFrame, m_NoneTexture);
      }

      uint32_t lastPipelineIdx = UINT32_MAX;
      lastMaterialIndex = UINT32_MAX;
      lastMaterialGen = UINT32_MAX;
      currentPipeline = nullptr;

      for (auto& dc : m_TransparentDrawCommands)
      {
        MaterialHandle matHandle { dc.materialIndex, dc.materialGeneration };
        MeshHandle meshHandle { dc.meshIndex, dc.meshGeneration };

        uint32_t pipelineIdx = (dc.instanced ? 2u : 0u) + (dc.doubleSided ? 1u : 0u);
        if (pipelineIdx != lastPipelineIdx)
        {
          currentPipeline = &GetWireframeTransparentPipeline(dc);
          currentPipeline->Bind(cmd);
          currentPipeline->BindDescriptorSets(cmd, {frameUBO}, 0);
          lastPipelineIdx = pipelineIdx;
          lastMaterialIndex = UINT32_MAX;
          lastMaterialGen = UINT32_MAX;
        }

        if (dc.materialIndex != lastMaterialIndex || dc.materialGeneration != lastMaterialGen)
        {
          currentPipeline->BindDescriptorSets(cmd,
            {materialManager.GetVulkanMaterial(matHandle).GetDescriptorSet(currentFrame)}, 1);
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
          m_InstanceBuffer.Update(dc.instanceOffset, dc.instanceData->data(),
            uint32_t(instanceCount * sizeof(glm::mat4)));
        }

        auto& vb = meshManager.GetVertexBuffer(meshHandle);
        m_Stats.drawCalls++;
        m_Stats.triangles += uint32_t(vb.GetIndexCount() / 3) * instanceCount;
        m_Stats.vertices += uint32_t(vb.GetIndexCount()) * instanceCount;
        vb.Draw(cmd, instanceCount);
      }
    }
  }

  void Render::SetUpCamera(FrameContext& frame)
  {
    auto& cam = frame.snapshot.camera;

    glm::mat4 world =
      glm::translate(glm::mat4(1.0f), cam.position) *
      glm::mat4_cast(cam.rotation);

    glm::mat4 view = glm::inverse(world);

    glm::mat4 proj = MakeReversedInfinitePerspective(cam.fov, cam.aspectRatio, cam.nearPlane);

    proj[1][1] *= -1.0f;

    // Store previous frame matrices (unjittered) for reprojection
    m_FrameUniformBuffer.uniforms.prevView = m_PrevView;
    m_FrameUniformBuffer.uniforms.prevProj = m_PrevProj;
    m_PrevView = view;
    m_PrevProj = proj;

    // Jitter without a resolve just makes the image crawl, and the indirect debug
    // views run with TAA forced off - see the matching block in Render::Draw.
    if (b_TAAEnabled && !IS_INDIRECT_DEBUG_VIEW(m_CurrentTexture))
    {
      glm::vec2 jitter = GetTAAJitter(m_GlobalFrameIndex);

      // Dividing by the full extent gives a +-0.25 px sweep, half the textbook +-0.5 px.
      // This is deliberate, not an oversight: measured on the racing scene, +-0.5 px raised
      // unstable pixels in the final image from 0.22% to 0.36% and widening the
      // reconstruction filter did not recover it. Narrower jitter trades a little
      // edge coverage for visibly less flicker.
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
      if (obj.isTransparent) continue;

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

#ifdef YA_EDITOR
  void Render::DrawPickIds(VkCommandBuffer cmd, uint32_t frameIndex, FrameContext& frame)
  {
    auto& slot = m_PickSlots[frameIndex];

    // Only the clicked texel is ever read back, so rasterization is clipped to it. The
    // vertex work still runs for everything visible, which is why the pass renders only
    // on frames that serve a request.
    VkRect2D scissor { { int32_t(slot.pixelX), int32_t(slot.pixelY) }, { 1, 1 } };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    auto& meshManager = frame.assets.Meshes();
    auto& materialManager = frame.assets.Materials();
    auto& cubeMapManager = frame.assets.CubeMaps();
    auto skybox = frame.snapshot.skybox;
    VkDescriptorSet frameUBO = m_FrameUniformBuffer.GetDescriptorSet(frameIndex);

    m_PickDrawCommands.clear();
    m_PickDrawCommands.reserve(frame.snapshot.visibleCount);
    for (uint32_t i = 0; i < frame.snapshot.visibleCount; i++)
    {
      auto& obj = frame.snapshot.objects[i];

      m_PickDrawCommands.push_back({
        .instanced = (obj.instanceData != nullptr),
        .doubleSided = obj.doubleSided,
        .noShading = obj.noShading,
        .isTerrain = obj.isTerrain,
        .isAlphaTest = obj.isAlphaTest,
        .isTransparent = obj.isTransparent,
        .materialIndex = obj.material.index,
        .materialGeneration = obj.material.generation,
        .meshIndex = obj.mesh.index,
        .meshGeneration = obj.mesh.generation,
        .worldTransform = obj.worldTransform,
        .instanceData = obj.instanceData,
        .instanceOffset = obj.instanceOffset,
        .entityId = obj.entityId,
      });
    }

    // Same order the visible passes used. Depth writes are off here, so more than one
    // surface can pass GEQUAL at a pixel - an unlit object in front of opaque geometry,
    // a transparent surface over both - and the last one drawn is the one on screen.
    std::sort(m_PickDrawCommands.begin(), m_PickDrawCommands.end(),
      [](const DrawCommand& a, const DrawCommand& b)
      {
        uint8_t ka = a.SortKey(), kb = b.SortKey();
        if (ka != kb) return ka < kb;
        if (a.materialIndex != b.materialIndex) return a.materialIndex < b.materialIndex;
        return a.meshIndex < b.meshIndex;
      });

    // Pre-bind materials needed by the alpha-test variants
    uint32_t lastMaterialIndex = UINT32_MAX;
    uint32_t lastMaterialGeneration = UINT32_MAX;
    for (auto& dc : m_PickDrawCommands)
    {
      if (!dc.isAlphaTest) continue;
      if (dc.materialIndex == lastMaterialIndex && dc.materialGeneration == lastMaterialGeneration) continue;
      lastMaterialIndex = dc.materialIndex;
      lastMaterialGeneration = dc.materialGeneration;

      MaterialHandle matHandle { dc.materialIndex, dc.materialGeneration };
      auto& mat = materialManager.Get(matHandle);
      mat.cubemap = skybox;
      materialManager.GetVulkanMaterial(matHandle).Bind(frame.assets.Textures(), cubeMapManager,
        frame.cubicResources, mat, frameIndex, m_NoneTexture);
    }

    VulkanPipeline* currentPipeline = nullptr;
    const VulkanPipeline* lastBound = nullptr;
    lastMaterialIndex = UINT32_MAX;
    lastMaterialGeneration = UINT32_MAX;

    for (auto& dc : m_PickDrawCommands)
    {
      MeshHandle meshHandle { dc.meshIndex, dc.meshGeneration };

      currentPipeline = &GetPickPipeline(dc);
      if (currentPipeline != lastBound)
      {
        currentPipeline->Bind(cmd);
        currentPipeline->BindDescriptorSets(cmd, {frameUBO}, 0);
        lastBound = currentPipeline;
        lastMaterialIndex = UINT32_MAX;
        lastMaterialGeneration = UINT32_MAX;
      }

      if (dc.isAlphaTest && (dc.materialIndex != lastMaterialIndex || dc.materialGeneration != lastMaterialGeneration))
      {
        MaterialHandle matHandle { dc.materialIndex, dc.materialGeneration };
        currentPipeline->BindDescriptorSets(cmd,
          {materialManager.GetVulkanMaterial(matHandle).GetDescriptorSet(frameIndex)}, 1);
        lastMaterialIndex = dc.materialIndex;
        lastMaterialGeneration = dc.materialGeneration;
      }

      struct
      {
        glm::mat4 model;
        int offset = 0;
        uint32_t pickId = 0;
      } data;
      data.model = dc.worldTransform;
      data.offset = dc.instanceOffset / sizeof(glm::mat4);
      // Zero is a valid entity handle and also the value the target is cleared to
      data.pickId = dc.entityId + 1;
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

  void Render::DrawMeshesBackfaceMask(VkCommandBuffer cmd, FrameContext& frame,
    VkDescriptorSet frameUBO)
  {
    auto& meshManager = frame.assets.Meshes();

    // Only closed opaque geometry may declare a node buried. Alpha-test and
    // double-sided surfaces are excluded because "inside" is meaningless for them:
    // a leaf card has no interior, and walking behind a foliage billboard must not
    // cost a node its capture. That also removes the alpha-test pipeline variants
    // and the material binding this pass would otherwise need.
    m_BackfaceDrawCommands.clear();
    m_BackfaceDrawCommands.reserve(frame.snapshot.visibleCount);
    for (uint32_t i = 0; i < frame.snapshot.visibleCount; i++)
    {
      auto& obj = frame.snapshot.objects[i];
      if (obj.noShading) continue;
      if (obj.isTransparent) continue;
      if (obj.isAlphaTest) continue;
      if (obj.doubleSided) continue;

      m_BackfaceDrawCommands.push_back({
        .instanced = (obj.instanceData != nullptr),
        .doubleSided = false,
        .noShading = false,
        .isAlphaTest = false,
        .materialIndex = obj.material.index,
        .materialGeneration = obj.material.generation,
        .meshIndex = obj.mesh.index,
        .meshGeneration = obj.mesh.generation,
        .worldTransform = obj.worldTransform,
        .instanceData = obj.instanceData,
        .instanceOffset = obj.instanceOffset,
      });
    }

    // Only the instanced bit picks a pipeline here, so sorting on it is enough to
    // keep the bind count at two for the whole pass.
    std::sort(m_BackfaceDrawCommands.begin(), m_BackfaceDrawCommands.end(),
      [](const DrawCommand& a, const DrawCommand& b)
      {
        if (a.instanced != b.instanced) return b.instanced;
        return a.meshIndex < b.meshIndex;
      });

    VulkanPipeline* currentPipeline = nullptr;
    const VulkanPipeline* lastBound = nullptr;

    for (auto& dc : m_BackfaceDrawCommands)
    {
      MeshHandle meshHandle { dc.meshIndex, dc.meshGeneration };

      currentPipeline = &m_PSOCache.Get(m_BackfaceMaskPipelines[dc.instanced ? 1 : 0]);
      if (currentPipeline != lastBound)
      {
        currentPipeline->Bind(cmd);
        currentPipeline->BindDescriptorSets(cmd, { frameUBO }, 0);
        lastBound = currentPipeline;
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
        currentPipeline->BindDescriptorSets(cmd, { m_InstanceDescriptorSet.Get() }, 1);
        m_InstanceBuffer.Update(dc.instanceOffset, dc.instanceData->data(),
          uint32_t(instanceCount * sizeof(glm::mat4)));
      }

      meshManager.GetVertexBuffer(meshHandle).DrawPositionOnly(cmd, instanceCount);
    }
  }
#endif

  bool Render::GrowMappedSlot(const RenderContext& ctx, VulkanBuffer& buffer,
    VkDeviceSize requiredBytes, VkDeviceSize capBytes, VkBufferUsageFlags usage,
    const char* name, bool& reportedFlag)
  {
    VkDeviceSize newSize = buffer.GetSize();
    while (newSize < requiredBytes && newSize < capBytes)
      newSize = std::min(newSize * 2, capBytes);

    bool grew = newSize > buffer.GetSize();
    if (grew)
    {
      // Nothing has been recorded into this slot yet this frame, and its previous
      // contents were retired behind the frame fence, so the old buffer can go now
      // instead of onto a deferred-destroy queue.
      buffer.Destroy(ctx);
      buffer = VulkanBuffer::CreateMapped(ctx, newSize, usage);
      YA_LOG_INFO("Render", "Shadow %s buffer grown to %llu KB",
        name, (unsigned long long)(newSize / 1024));
    }

    if (newSize < requiredBytes && !reportedFlag)
    {
      reportedFlag = true;
      YA_LOG_WARN("Render",
        "Shadow %s buffer capped at %llu KB while %llu KB was requested, the excess draws are dropped",
        name, (unsigned long long)(newSize / 1024), (unsigned long long)(requiredBytes / 1024));
    }

    return grew;
  }

  void Render::RenderShadowMaps(FrameContext& frame, VkCommandBuffer cmd,
    uint32_t frameIndex, const glm::vec3* probeCenter, float probeRadius)
  {
    auto& ctx = m_Backend.GetContext();

    // Support is a property of the device and the toggle is a user setting, so the
    // effective path is decided here every frame rather than latched at load time.
    bool useIndirect = b_ShadowIndirectEnabled
      && ctx.multiDrawIndirectSupported
      && ctx.drawIndirectFirstInstanceSupported
      && ctx.geometryArena != nullptr;

    // Bakes render the atlas on a single-time command buffer outside the frame loop
    // and pass frameIndex 0, which the frame loop may still own. The passed index is
    // deliberately ignored for them and the dedicated bake slot is used instead.
    uint32_t shadowSlot = GetShadowSlot(frameIndex, probeCenter != nullptr);

    // Collect shadow draw commands from ALL objects (not just camera-visible).
    // Objects outside the camera frustum can still cast shadows into the view.
    m_ShadowDrawCommands.clear();
    uint32_t totalCount = uint32_t(frame.snapshot.objects.size());
    m_ShadowDrawCommands.reserve(totalCount);
    for (uint32_t i = 0; i < totalCount; i++)
    {
      auto& obj = frame.snapshot.objects[i];
      if (obj.noShading) continue;
      if (obj.isTransparent) continue;

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

    // The indirect path needs the two opaque cull-mode groups contiguous, which the
    // legacy key does not give (it splits opaque four ways on instancing as well).
    // The legacy path keeps its own key so its pipeline bind count is unchanged.
    if (useIndirect)
    {
      std::sort(m_ShadowDrawCommands.begin(), m_ShadowDrawCommands.end(),
        [](const DrawCommand& a, const DrawCommand& b)
        {
          uint8_t ka = a.ShadowBatchKey(), kb = b.ShadowBatchKey();
          if (ka != kb) return ka < kb;
          if (a.materialIndex != b.materialIndex) return a.materialIndex < b.materialIndex;
          return a.meshIndex < b.meshIndex;
        });
    }
    else
    {
      std::sort(m_ShadowDrawCommands.begin(), m_ShadowDrawCommands.end(),
        [](const DrawCommand& a, const DrawCommand& b)
        {
          uint8_t ka = a.SortKey(), kb = b.SortKey();
          if (ka != kb) return ka < kb;
          if (a.materialIndex != b.materialIndex) return a.materialIndex < b.materialIndex;
          return a.meshIndex < b.meshIndex;
        });
    }

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
      materialManager.GetVulkanMaterial(matHandle).Bind(frame.assets.Textures(), cubeMapManager, frame.cubicResources, mat, frameIndex, m_NoneTexture);
    }

    bool hasDirectionalShadow = b_ShadowsEnabled && frame.snapshot.directionalShadow.castShadow;
    bool hasSpotShadows = b_ShadowsEnabled && !frame.snapshot.spotShadowRequests.empty();
    bool hasPointShadows = b_ShadowsEnabled && !frame.snapshot.pointShadowRequests.empty();

    if (!hasDirectionalShadow && !hasSpotShadows && !hasPointShadows)
    {
      m_ShadowManager.SetEnabled(false);
      m_ShadowManager.SetSpotShadowCount(0);
      m_ShadowManager.SetPointShadowCount(0);
      m_ShadowManager.SetUp(frameIndex);
#ifdef YA_EDITOR
      if (b_ShadowBreakdownPending && !probeCenter)
      {
        m_ShadowBreakdownTriangles.clear();
        DumpShadowBreakdown();
      }
#endif
      return;
    }

    // Compute shadow matrices
    if (hasDirectionalShadow)
    {
      auto& cam = frame.snapshot.camera;
      auto& shadow = frame.snapshot.directionalShadow;
      m_ShadowManager.SetEnabled(true);
      if (probeCenter)
      {
        m_ShadowManager.ComputeCascadesAroundPoint(
          *probeCenter,
          PROBE_SHADOW_NEAR_PLANE,
          shadow.shadowDistance,
          shadow.direction,
          probeRadius);
      }
      else
      {
        m_ShadowManager.ComputeCascades(
          m_FrameUniformBuffer.uniforms.view,
          cam.fov, cam.aspectRatio,
          cam.nearPlane, cam.farPlane,
          shadow.shadowDistance,
          shadow.direction);
      }

      // A degenerate shadow distance makes both of them switch shadows off without
      // writing a single cascade matrix. Rendering the tiles anyway would feed zero
      // matrices into ExtractFrustumPlanes and divide by a zero-length normal.
      hasDirectionalShadow = m_ShadowManager.IsEnabled();
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

    auto currentFrame = frameIndex;
    m_ShadowManager.SetUp(currentFrame);

    auto& meshManager = frame.assets.Meshes();

    // Ranges of m_ShadowDrawCommands that share a cull mode, one indirect draw each.
    size_t batchBegin[2] = { 0, 0 };
    size_t batchEnd[2] = { 0, 0 };
    uint32_t indirectCursor = 0;
    uint32_t indirectCapacity = 0;
    uint32_t modelCapacity = 0;
    VkBuffer indirectBuffer = VK_NULL_HANDLE;
    VkDrawIndexedIndirectCommand* indirectCommands = nullptr;

    if (useIndirect)
    {
      size_t commandCount = m_ShadowDrawCommands.size();
      m_ShadowIndirectRecords.assign(commandCount, ShadowIndirectRecord {});

      // Pass one resolves the arena range and instance count of every opaque caster,
      // which is all the model SSBO sizing needs.
      uint64_t requiredInstances = 0;
      uint32_t batchableCount = 0;
      for (size_t i = 0; i < commandCount; i++)
      {
        auto& dc = m_ShadowDrawCommands[i];
        if (dc.isAlphaTest)
          continue;

        MeshHandle meshHandle { dc.meshIndex, dc.meshGeneration };
        auto& vb = meshManager.GetVertexBuffer(meshHandle);
        if (!vb.IsArenaResident())
          continue;

        const GeometryArenaAllocation& alloc = vb.GetArenaAllocation();
        auto& rec = m_ShadowIndirectRecords[i];
        rec.batchable = true;
        rec.indexCount = alloc.indexCount;
        rec.firstIndex = alloc.firstIndex;
        rec.vertexOffset = int32_t(alloc.vertexOffset);
        rec.instanceCount = dc.instanced ? uint32_t(dc.instanceData->size()) : 1u;
        requiredInstances += rec.instanceCount;
        batchableCount++;
      }

      // Rewritten only when the handle actually changed: the set is still bound by the
      // command buffers of the other slots and there is no reason to touch it otherwise.
      if (GrowMappedSlot(ctx, m_ShadowModelBuffers[shadowSlot],
        requiredInstances * sizeof(glm::mat4), SHADOW_MODEL_CAP_BYTES,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "model", b_ShadowModelOverflowReported))
      {
        m_ShadowModelDescriptorSets[shadowSlot].WriteStorageBuffer(0,
          m_ShadowModelBuffers[shadowSlot].Get(), m_ShadowModelBuffers[shadowSlot].GetSize());
      }
      modelCapacity = uint32_t(m_ShadowModelBuffers[shadowSlot].GetSize() / sizeof(glm::mat4));

      // Pass two lays the final world matrices out in sorted order. Instanced casters
      // are premultiplied here because the indirect shader reads one matrix and cannot
      // do the pc.world * instance product the legacy shader did; it happens once per
      // frame rather than once per atlas tile.
      auto* models = static_cast<glm::mat4*>(m_ShadowModelBuffers[shadowSlot].GetMapped());
      uint32_t modelCursor = 0;
      for (size_t i = 0; i < commandCount; i++)
      {
        auto& rec = m_ShadowIndirectRecords[i];
        if (!rec.batchable)
          continue;

        if (modelCursor + rec.instanceCount > modelCapacity)
        {
          rec.batchable = false;
          batchableCount--;
          continue;
        }

        auto& dc = m_ShadowDrawCommands[i];
        rec.modelBase = modelCursor;
        if (dc.instanced)
        {
          const auto& instances = *dc.instanceData;
          for (uint32_t n = 0; n < rec.instanceCount; n++)
            models[modelCursor + n] = dc.worldTransform * instances[n];
        }
        else
        {
          models[modelCursor] = dc.worldTransform;
        }
        modelCursor += rec.instanceCount;
      }

      // These host writes become visible to the device through the implicit host-write
      // barrier of vkQueueSubmit. No explicit flush or memory barrier is needed here.

      m_ShadowLegacyIndices.clear();
      for (size_t i = 0; i < commandCount; i++)
      {
        if (!m_ShadowIndirectRecords[i].batchable)
          m_ShadowLegacyIndices.push_back(uint32_t(i));
      }
      // Restores the legacy ordering for the draws that still go one by one, so the
      // alpha-test tail binds as few pipelines and materials as it does today.
      std::sort(m_ShadowLegacyIndices.begin(), m_ShadowLegacyIndices.end(),
        [this](uint32_t a, uint32_t b)
        {
          const DrawCommand& ca = m_ShadowDrawCommands[a];
          const DrawCommand& cb = m_ShadowDrawCommands[b];
          uint8_t ka = ca.SortKey(), kb = cb.SortKey();
          if (ka != kb) return ka < kb;
          if (ca.materialIndex != cb.materialIndex) return ca.materialIndex < cb.materialIndex;
          return ca.meshIndex < cb.meshIndex;
        });

      size_t scan = 0;
      while (scan < commandCount && m_ShadowDrawCommands[scan].ShadowBatchKey() == 0)
        scan++;
      batchEnd[0] = scan;
      batchBegin[1] = scan;
      while (scan < commandCount && m_ShadowDrawCommands[scan].ShadowBatchKey() == 1)
        scan++;
      batchEnd[1] = scan;

      uint32_t tileCount = (hasDirectionalShadow ? CSM_CASCADE_COUNT : 0)
        + uint32_t(frame.snapshot.spotShadowRequests.size())
        + uint32_t(frame.snapshot.pointShadowRequests.size()) * 6;

      GrowMappedSlot(ctx, m_ShadowIndirectBuffers[shadowSlot],
        VkDeviceSize(tileCount) * batchableCount * sizeof(VkDrawIndexedIndirectCommand),
        SHADOW_INDIRECT_CAP_BYTES, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, "indirect",
        b_ShadowIndirectOverflowReported);
      indirectBuffer = m_ShadowIndirectBuffers[shadowSlot].Get();
      indirectCommands = static_cast<VkDrawIndexedIndirectCommand*>(
        m_ShadowIndirectBuffers[shadowSlot].GetMapped());
      indirectCapacity = uint32_t(m_ShadowIndirectBuffers[shadowSlot].GetSize()
        / sizeof(VkDrawIndexedIndirectCommand));
    }

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

    // Upload instance data once before all shadow passes
    for (auto& dc : m_ShadowDrawCommands)
    {
      if (dc.instanced)
      {
        m_InstanceBuffer.Update(dc.instanceOffset, dc.instanceData->data(),
          uint32_t(dc.instanceData->size() * sizeof(glm::mat4)));
      }
    }

    // Helper lambda: draw all shadow commands through the given tile projection.
    // frustumPlanes + planeCount for frustum culling; nullptr disables frustum culling.
    // tileTriangles accumulates what actually reaches a draw, and perDrawTriangles -
    // when armed - splits the same total across m_ShadowDrawCommands for the dump.
    auto drawShadowPass = [&](const glm::mat4& viewProj, const ShadowViewport& sv,
      uint32_t* tileTriangles, const FrustumPlane* frustumPlanes = nullptr, int planeCount = 6,
      uint32_t* perDrawTriangles = nullptr)
    {
      vkCmdSetViewport(cmd, 0, 1, &sv.viewport);
      vkCmdSetScissor(cmd, 0, 1, &sv.scissor);

      auto countTriangles = [&](size_t commandIndex, uint32_t indexCount, uint32_t instanceCount)
      {
        uint32_t triangles = (indexCount / 3) * instanceCount;
        *tileTriangles += triangles;
        m_Stats.shadowTriangles += triangles;
        if (perDrawTriangles)
          perDrawTriangles[commandIndex] += triangles;
      };

      if (useIndirect)
      {
        for (uint32_t cullMode = 0; cullMode < 2; cullMode++)
        {
          uint32_t rangeFirst = indirectCursor;

          for (size_t i = batchBegin[cullMode]; i < batchEnd[cullMode]; i++)
          {
            auto& rec = m_ShadowIndirectRecords[i];
            if (!rec.batchable)
              continue;

            // Same test, same planes, same plane count as the legacy loop below:
            // the cascades pass 5 planes on purpose and passing 6 here would clip
            // distant casters out of them.
            auto& dc = m_ShadowDrawCommands[i];
            bool hasBounds = dc.boundsMin.x <= dc.boundsMax.x;
            if (hasBounds && frustumPlanes)
            {
              if (!IsAABBVisible(dc.boundsMin, dc.boundsMax, frustumPlanes, planeCount))
                continue;
            }

            if (indirectCursor >= indirectCapacity)
            {
              if (!b_ShadowIndirectOverflowReported)
              {
                b_ShadowIndirectOverflowReported = true;
                YA_LOG_WARN("Render",
                  "Shadow indirect command buffer full at %u commands, the rest of the atlas is not drawn",
                  indirectCapacity);
              }
              break;
            }

            VkDrawIndexedIndirectCommand& out = indirectCommands[indirectCursor++];
            out.indexCount = rec.indexCount;
            out.instanceCount = rec.instanceCount;
            out.firstIndex = rec.firstIndex;
            out.vertexOffset = rec.vertexOffset;
            // Picks the batch's first model matrix; gl_InstanceIndex adds the rest.
            out.firstInstance = rec.modelBase;

            countTriangles(i, rec.indexCount, rec.instanceCount);
          }

          uint32_t commandCount = indirectCursor - rangeFirst;
          if (commandCount == 0)
            continue;

          auto& pipeline = m_PSOCache.Get(m_ShadowIndirectPipelines[cullMode]);
          pipeline.Bind(cmd);
          pipeline.BindDescriptorSets(cmd,
            { m_ShadowManager.GetShadowCascadeUBODescriptorSet(currentFrame) }, 0);
          pipeline.BindDescriptorSets(cmd,
            { m_ShadowModelDescriptorSets[shadowSlot].Get() }, 1);

          glm::mat4 tileViewProj = viewProj;
          pipeline.PushConstants(cmd, &tileViewProj);

          // Re-bound every tile because the alpha-test tail below binds its own
          // interleaved vertex stream over these.
          VkBuffer positions = ctx.geometryArena->GetPositionBuffer();
          VkDeviceSize positionOffset = 0;
          vkCmdBindVertexBuffers(cmd, 0, 1, &positions, &positionOffset);
          vkCmdBindIndexBuffer(cmd, ctx.geometryArena->GetIndexBuffer(VK_INDEX_TYPE_UINT32), 0,
            VK_INDEX_TYPE_UINT32);

          uint32_t maxPerCall = std::max(ctx.maxDrawIndirectCount, 1u);
          if (commandCount > maxPerCall && !b_ShadowIndirectCountSplitReported)
          {
            b_ShadowIndirectCountSplitReported = true;
            YA_LOG_WARN("Render",
              "Shadow indirect range of %u commands exceeds maxDrawIndirectCount %u, splitting",
              commandCount, maxPerCall);
          }

          for (uint32_t issued = 0; issued < commandCount; issued += maxPerCall)
          {
            uint32_t batch = std::min(maxPerCall, commandCount - issued);
            vkCmdDrawIndexedIndirect(cmd, indirectBuffer,
              VkDeviceSize(rangeFirst + issued) * sizeof(VkDrawIndexedIndirectCommand),
              batch, sizeof(VkDrawIndexedIndirectCommand));
            m_Stats.shadowDrawCalls++;
          }
          m_Stats.shadowIndirectCommands += commandCount;
        }
      }

      uint8_t lastSortKey = UINT8_MAX;
      uint32_t lastMaterialIndex = UINT32_MAX;
      uint32_t lastMaterialGen = UINT32_MAX;
      VulkanPipeline* currentPipeline = nullptr;

      // On the indirect path this is the alpha-test tail plus anything the arena could
      // not take; otherwise it is the whole pass, unchanged.
      size_t legacyCount = useIndirect ? m_ShadowLegacyIndices.size() : m_ShadowDrawCommands.size();
      for (size_t legacyIndex = 0; legacyIndex < legacyCount; legacyIndex++)
      {
        size_t commandIndex = useIndirect ? m_ShadowLegacyIndices[legacyIndex] : legacyIndex;
        auto& dc = m_ShadowDrawCommands[commandIndex];
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
          glm::mat4 viewProjWorld;
          int offset = 0;
        } data;
        // The shader used to redo this product per vertex against a dynamically
        // indexed UBO; the tile projection is uniform over the draw, so it is folded
        // into the model matrix here instead.
        data.viewProjWorld = viewProj * dc.worldTransform;
        data.offset = dc.instanceOffset / sizeof(glm::mat4);
        currentPipeline->PushConstants(cmd, &data);

        uint32_t instanceCount = 1;
        if (dc.instanced)
        {
          instanceCount = uint32_t(dc.instanceData->size());
          uint32_t instanceSetIndex = dc.isAlphaTest ? 2 : 1;
          currentPipeline->BindDescriptorSets(cmd, { m_InstanceDescriptorSet.Get() }, instanceSetIndex);
        }

        auto& vb = meshManager.GetVertexBuffer(meshHandle);
        m_Stats.shadowDrawCalls++;
        countTriangles(commandIndex, uint32_t(vb.GetIndexCount()), instanceCount);
        if (dc.isAlphaTest)
          vb.Draw(cmd, instanceCount);
        else
          vb.DrawPositionOnly(cmd, instanceCount);
      }
    };

    auto& shadowData = m_ShadowManager.GetShadowData();

    // Every tile gets its own label so a GPU profiler can price cascades and lights
    // one by one. DebugMarker is already a no-op outside debug builds.
    char tileLabel[32];

    uint32_t* breakdownRows = nullptr;
#ifdef YA_EDITOR
    if (b_ShadowBreakdownPending && !probeCenter)
    {
      m_ShadowBreakdownTriangles.assign(size_t(CSM_CASCADE_COUNT) * m_ShadowDrawCommands.size(), 0u);
      breakdownRows = m_ShadowBreakdownTriangles.data();
    }
#endif

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
        uint32_t* perDraw = breakdownRows
          ? breakdownRows + size_t(cascade) * m_ShadowDrawCommands.size()
          : nullptr;

        snprintf(tileLabel, sizeof(tileLabel), "Cascade %u", cascade);
        DebugMarker::BeginLabel(cmd, tileLabel);
        drawShadowPass(shadowData.cascades[cascade].viewProj, sv,
          &m_Stats.shadowTrianglesPerCascade[cascade], csmPlanes, 5, perDraw);
        DebugMarker::EndLabel(cmd);
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

      snprintf(tileLabel, sizeof(tileLabel), "Spot %u", i);
      DebugMarker::BeginLabel(cmd, tileLabel);
      drawShadowPass(shadowData.spotShadows[i].viewProj, sv,
        &m_Stats.shadowTrianglesPerSpot[i], spotPlanes, 6);
      DebugMarker::EndLabel(cmd);
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

        snprintf(tileLabel, sizeof(tileLabel), "Point %u Face %u", i, face);
        DebugMarker::BeginLabel(cmd, tileLabel);
        drawShadowPass(shadowData.pointShadows[i].faceViewProj[face], sv,
          &m_Stats.shadowTrianglesPerPoint[i], facePlanes, 6);
        DebugMarker::EndLabel(cmd);
      }
    }
    if (hasPointShadows)
      DebugMarker::EndLabel(cmd);

    vkCmdEndRenderPass(cmd);
    DebugMarker::EndLabel(cmd);

#ifdef YA_EDITOR
    if (b_ShadowBreakdownPending && !probeCenter)
      DumpShadowBreakdown();
#endif
  }

#ifdef YA_EDITOR
  void Render::DumpShadowBreakdown()
  {
    b_ShadowBreakdownPending = false;

    uint32_t worstCascade = 0;
    for (uint32_t cascade = 1; cascade < CSM_CASCADE_COUNT; cascade++)
    {
      if (m_Stats.shadowTrianglesPerCascade[cascade] > m_Stats.shadowTrianglesPerCascade[worstCascade])
        worstCascade = cascade;
    }

    uint32_t cascadeTriangles = m_Stats.shadowTrianglesPerCascade[worstCascade];
    size_t commandCount = m_ShadowDrawCommands.size();
    if (cascadeTriangles == 0 || m_ShadowBreakdownTriangles.size() < size_t(CSM_CASCADE_COUNT) * commandCount)
    {
      YA_LOG_INFO("Render", "Shadow breakdown: no cascade geometry was submitted this frame");
      m_ShadowBreakdownTriangles.clear();
      return;
    }

    // Draw commands are per caster, so one mesh usually appears many times over. The
    // question the dump answers is which mesh the cascade spends its triangles on.
    struct MeshCost
    {
      uint32_t meshIndex;
      uint32_t meshGeneration;
      uint32_t triangles;
      uint32_t drawCount;
    };
    std::vector<MeshCost> costs;
    std::unordered_map<uint64_t, size_t> byMesh;

    const uint32_t* row = m_ShadowBreakdownTriangles.data() + size_t(worstCascade) * commandCount;
    for (size_t i = 0; i < commandCount; i++)
    {
      if (row[i] == 0)
        continue;

      const DrawCommand& dc = m_ShadowDrawCommands[i];
      uint64_t key = (uint64_t(dc.meshIndex) << 32) | dc.meshGeneration;
      auto [it, inserted] = byMesh.try_emplace(key, costs.size());
      if (inserted)
        costs.push_back({ dc.meshIndex, dc.meshGeneration, 0, 0 });

      MeshCost& cost = costs[it->second];
      cost.triangles += row[i];
      cost.drawCount++;
    }

    size_t reported = std::min<size_t>(costs.size(), 20);
    std::partial_sort(costs.begin(), costs.begin() + reported, costs.end(),
      [](const MeshCost& a, const MeshCost& b) { return a.triangles > b.triangles; });

    YA_LOG_INFO("Render",
      "Shadow breakdown: cascade %u is the heaviest with %u triangles from %zu meshes, top %zu:",
      worstCascade, cascadeTriangles, costs.size(), reported);

    // Meshes carry no name, so the handle is the only stable identifier here.
    for (size_t i = 0; i < reported; i++)
    {
      const MeshCost& cost = costs[i];
      YA_LOG_INFO("Render", "  %2zu. mesh %u:%u  %u tris  %.1f%%  in %u draws",
        i + 1, cost.meshIndex, cost.meshGeneration, cost.triangles,
        100.0 * double(cost.triangles) / double(cascadeTriangles), cost.drawCount);
    }

    m_ShadowBreakdownTriangles.clear();
  }
#endif

  void Render::DrawQuad(VkCommandBuffer cmd)
  {
    vkCmdDraw(cmd, 3, 1, 0, 0);
  }

  void Render::SubmitParticles(std::span<const ParticleInstance> particles, TextureHandle texture)
  {
    if (particles.empty() || !texture)
      return;
    if (m_PendingParticleBatches.size() >= MAX_PARTICLE_BATCHES_PER_FRAME)
      return;

    const uint32_t remaining = MAX_PARTICLES_PER_FRAME - uint32_t(m_ParticleStage.size());
    if (remaining == 0)
      return;

    const uint32_t count = std::min(uint32_t(particles.size()), remaining);
    const uint32_t firstInstance = uint32_t(m_ParticleStage.size());
    m_ParticleStage.insert(m_ParticleStage.end(), particles.begin(), particles.begin() + count);
    m_PendingParticleBatches.push_back({ firstInstance, count, texture });
  }

  void Render::DrawTransparent(VkCommandBuffer cmd, uint32_t frameIndex, FrameContext& frame)
  {
    // Wireframe debug draws transparent geometry inside GBuffer pass instead
    if (m_CurrentTexture == DEBUG_VIEW_WIREFRAME)
    {
      m_ParticleStage.clear();
      m_PendingParticleBatches.clear();
      return;
    }

    if (m_TransparentDrawCommands.empty() && m_PendingParticleBatches.empty())
      return;

    auto currentFrame = frameIndex;
    VkDescriptorSet frameUBO = m_FrameUniformBuffer.GetDescriptorSet(currentFrame);
    auto& meshManager = frame.assets.Meshes();
    auto& materialManager = frame.assets.Materials();
    auto& cubeMapManager = frame.assets.CubeMaps();
    auto skybox = frame.snapshot.skybox;

    // Back-to-front sort by squared distance to camera
    std::sort(m_TransparentDrawCommands.begin(), m_TransparentDrawCommands.end(),
      [](const DrawCommand& a, const DrawCommand& b)
      {
        return a.cameraDistanceSq > b.cameraDistanceSq;
      });

    // Pre-bind all transparent materials (texture upload + descriptor write)
    uint32_t preLastMaterialIndex = UINT32_MAX;
    uint32_t preLastMaterialGen = UINT32_MAX;
    for (auto& dc : m_TransparentDrawCommands)
    {
      if (dc.materialIndex == preLastMaterialIndex && dc.materialGeneration == preLastMaterialGen) continue;
      preLastMaterialIndex = dc.materialIndex;
      preLastMaterialGen = dc.materialGeneration;

      MaterialHandle matHandle { dc.materialIndex, dc.materialGeneration };
      auto& mat = materialManager.Get(matHandle);
      mat.cubemap = skybox;
      materialManager.GetVulkanMaterial(matHandle).Bind(frame.assets.Textures(), cubeMapManager, frame.cubicResources, mat, currentFrame, m_NoneTexture);
    }

    uint32_t lastPipelineIdx = UINT32_MAX;
    uint32_t lastMaterialIndex = UINT32_MAX;
    uint32_t lastMaterialGen = UINT32_MAX;
    VulkanPipeline* currentPipeline = nullptr;

    for (auto& dc : m_TransparentDrawCommands)
    {
      MaterialHandle matHandle { dc.materialIndex, dc.materialGeneration };
      MeshHandle meshHandle { dc.meshIndex, dc.meshGeneration };

      uint32_t pipelineIdx = (dc.instanced ? 2u : 0u) + (dc.doubleSided ? 1u : 0u);
      if (pipelineIdx != lastPipelineIdx)
      {
        currentPipeline = &GetForwardTransparentPipeline(dc);
        currentPipeline->Bind(cmd);
        currentPipeline->BindDescriptorSets(cmd, {frameUBO}, 0);
        currentPipeline->BindDescriptorSets(cmd, {m_DeferredLightingLightDescriptorSets[currentFrame].Get()}, 2);
        currentPipeline->BindDescriptorSets(cmd, {m_IBLDescriptorSets[currentFrame].Get()}, 3);
        lastPipelineIdx = pipelineIdx;
        lastMaterialIndex = UINT32_MAX;
        lastMaterialGen = UINT32_MAX;
      }

      if (dc.materialIndex != lastMaterialIndex || dc.materialGeneration != lastMaterialGen)
      {
        currentPipeline->BindDescriptorSets(cmd,
          {materialManager.GetVulkanMaterial(matHandle).GetDescriptorSet(currentFrame)}, 1);
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
        currentPipeline->BindDescriptorSets(cmd, { m_InstanceDescriptorSet.Get() }, 4);
        m_InstanceBuffer.Update(dc.instanceOffset, dc.instanceData->data(),
          uint32_t(instanceCount * sizeof(glm::mat4)));
      }

      auto& vb = meshManager.GetVertexBuffer(meshHandle);
      m_Stats.drawCalls++;
      m_Stats.triangles += uint32_t(vb.GetIndexCount() / 3) * instanceCount;
      m_Stats.vertices += uint32_t(vb.GetIndexCount()) * instanceCount;
      vb.Draw(cmd, instanceCount);
    }

    if (!m_PendingParticleBatches.empty())
    {
      DebugMarker::BeginLabel(cmd, "Particles", 1.0f, 0.6f, 0.2f);

      m_ParticleInstanceBuffers[currentFrame].Update(0,
        m_ParticleStage.data(),
        uint32_t(m_ParticleStage.size() * sizeof(ParticleInstance)));

      auto& particlePipeline = m_PSOCache.Get(m_ParticlePipeline);
      particlePipeline.Bind(cmd);
      particlePipeline.BindDescriptorSets(cmd, { frameUBO }, 0);

      auto& textureManager = frame.assets.Textures();
      uint32_t batchIdx = 0;
      for (auto& batch : m_PendingParticleBatches)
      {
        auto& tex = textureManager.GetVulkanTexture(batch.texture);
        uint32_t setIdx = currentFrame * MAX_PARTICLE_BATCHES_PER_FRAME + batchIdx;
        auto& descSet = m_ParticleDescriptorSets[setIdx];
        descSet.WriteCombinedImageSampler(1, tex.GetView(), tex.GetSampler());
        particlePipeline.BindDescriptorSets(cmd, { descSet.Get() }, 1);

        vkCmdDraw(cmd, 4, batch.count, 0, batch.firstInstance);

        m_Stats.drawCalls++;
        m_Stats.triangles += 2 * batch.count;
        m_Stats.vertices += 4 * batch.count;
        batchIdx++;
      }

      DebugMarker::EndLabel(cmd);
    }

    m_ParticleStage.clear();
    m_PendingParticleBatches.clear();
  }
}
