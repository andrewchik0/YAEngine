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
  namespace
  {
    // Restoring transform (translate(bias)*scale(scale)) is diagonal+translation, so folding it
    // into model scales 3 columns and shifts the 4th instead of a full 4x4 product; runs once per instance per frame.
    glm::mat4 FoldDequantization(const glm::mat4& model, const glm::vec3& scale, const glm::vec3& bias)
    {
      glm::mat4 result;
      result[3] = model[0] * bias.x + model[1] * bias.y + model[2] * bias.z + model[3];
      result[0] = model[0] * scale.x;
      result[1] = model[1] * scale.y;
      result[2] = model[2] * scale.z;
      return result;
    }

    // FNV-1a for the shadow cache's settings digest. Mirrors SnapshotDigest in
    // SceneSnapshot.h; kept local on both sides because a shared hashing
    // utility is not warranted for two ten-line folds.
    struct Fnv1a
    {
      uint64_t value = 14695981039346656037ull;

      template<typename T>
      void Add(const T& data)
      {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&data);
        for (size_t i = 0; i < sizeof(T); i++)
        {
          value ^= bytes[i];
          value *= 1099511628211ull;
        }
      }
    };

    // Padding applied to every dirty rect. The PCF kernel reads neighboring
    // texels, so an unpadded rect edge would leave a seam of stale texels the
    // kernel still samples.
    constexpr int32_t SHADOW_RECT_PAD_TEXELS = 4;
    // Above this fraction of the tile area a rect update stops paying off and
    // the whole tile is redrawn instead.
    constexpr float SHADOW_RECT_MAX_TILE_FRACTION = 0.5f;

    // Remaps the NDC sub-rect [ndcMin, ndcMax] onto the full [-1,1] range.
    // Multiplied in front of a tile viewProj it yields the frustum of exactly
    // that rect: left/right/top/bottom tighten while the near/far rows pass
    // through untouched, so the tile's plane-count convention is preserved.
    glm::mat4 MakeNdcCropMatrix(const glm::vec2& ndcMin, const glm::vec2& ndcMax)
    {
      glm::vec2 scale = glm::vec2(2.0f) / (ndcMax - ndcMin);
      glm::vec2 offset = -(ndcMax + ndcMin) / (ndcMax - ndcMin);
      glm::mat4 crop(1.0f);
      crop[0][0] = scale.x;
      crop[1][1] = scale.y;
      crop[3][0] = offset.x;
      crop[3][1] = offset.y;
      return crop;
    }
  }

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
    MeshBindCache bindCache;

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
        vb.Draw(cmd, instanceCount, &bindCache);
      else
        vb.DrawPositionOnly(cmd, instanceCount, 0, &bindCache);
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
    MeshBindCache bindCache;
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
        vb.Draw(cmd, instanceCount, &bindCache);
      else
        vb.DrawPositionOnly(cmd, instanceCount, 0, &bindCache);
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
    MeshBindCache bindCache;

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

      meshManager.GetVertexBuffer(meshHandle).DrawPositionOnly(cmd, instanceCount, 0, &bindCache);
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

    // Support is a property of the device, so the effective path is decided here
    // rather than latched at load time.
    bool useIndirect = ctx.multiDrawIndirectSupported
      && ctx.drawIndirectFirstInstanceSupported
      && ctx.geometryArena != nullptr;

    // Quantized positions belong to the indirect path alone. Every other consumer of
    // the arena - the depth prepass above all - must keep reading the exact stream,
    // because the prepass writes the depth the G-buffer then tests against.
    bool useQuantizedPositions = useIndirect && ctx.unorm16VertexSupported;

    // Bakes rewrite the whole atlas with different matrices, so they clear it whole;
    // every other frame loads it and clears only the tiles it is about to draw.
    const bool isBake = probeCenter != nullptr;

    // Bakes render the atlas on a single-time command buffer outside the frame loop
    // and pass frameIndex 0, which the frame loop may still own. The passed index is
    // deliberately ignored for them and the dedicated bake slot is used instead.
    uint32_t shadowSlot = GetShadowSlot(frameIndex, isBake);

    bool hasDirectionalShadow = b_ShadowsEnabled && frame.snapshot.directionalShadow.castShadow;
    bool hasSpotShadows = b_ShadowsEnabled && !frame.snapshot.spotShadowRequests.empty();
    bool hasPointShadows = b_ShadowsEnabled && !frame.snapshot.pointShadowRequests.empty();

    if (!hasDirectionalShadow && !hasSpotShadows && !hasPointShadows)
    {
      m_ShadowManager.SetEnabled(false);
      m_ShadowManager.SetSpotShadowCount(0);
      m_ShadowManager.SetPointShadowCount(0);
      m_ShadowManager.SetUp(frameIndex);
      if (!probeCenter)
      {
        // The atlas still holds the last shadow-casting frame; it must not be
        // reused verbatim when shadows come back on.
        b_ShadowAtlasContentValid = false;
        m_ShadowCachePendingReason = ShadowInvalidation::ShadowsToggled;
      }
      return;
    }

    // Compute shadow matrices. This block reads only the snapshot and the frame
    // UBO, so it runs before caster collection on purpose: the cache decision
    // below needs the refit outcome without paying for the collection first.
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

    // Everything the cache decision compares this frame, captured once so the
    // post-render store writes exactly the values that were compared.
    uint64_t shadowGeometryVersion = ctx.geometryArena ? ctx.geometryArena->GetContentVersion() : 0;
    uint32_t requestedSpotShadows = uint32_t(frame.snapshot.spotShadowRequests.size());
    uint32_t requestedPointShadows = uint32_t(frame.snapshot.pointShadowRequests.size());
    uint64_t shadowSettingsDigest;
    {
      Fnv1a digest;
      digest.Add(b_ShadowsEnabled);
      digest.Add(b_ShadowLodEnabled);
      for (int lod : m_ShadowCascadeLods)
        digest.Add(lod);
      shadowSettingsDigest = digest.value;
    }

    // Which tiles this frame draws. Bakes and full rebuilds take every tile;
    // the partial decision narrows this to the refitted cascades, the rect
    // decision to the tiles a mover's footprint touches.
    bool partialRebuild = false;
    bool rectRebuild = false;
    bool cascadeDirty[CSM_CASCADE_COUNT];
    std::fill(std::begin(cascadeDirty), std::end(cascadeDirty), true);
    bool spotTileDirty[MAX_SHADOW_SPOTS];
    std::fill(std::begin(spotTileDirty), std::end(spotTileDirty), true);
    bool pointFaceDirty[MAX_SHADOW_POINTS][6];
    for (auto& faces : pointFaceDirty)
      std::fill(std::begin(faces), std::end(faces), true);
    // A dirty cascade tile with hasRect redraws only this atlas rect, else the
    // whole tile. ndcMin/ndcMax is the same rect in tile NDC, feeding the
    // crop-cull matrix in the cascade loop below.
    struct CascadeDirtyRect
    {
      bool hasRect = false;
      VkRect2D rect {};
      glm::vec2 ndcMin { 0.0f };
      glm::vec2 ndcMax { 0.0f };
    };
    CascadeDirtyRect cascadeRects[CSM_CASCADE_COUNT];

    // All-or-nothing cache: when nothing shadow-relevant changed since the last
    // rendered frame, the atlas already holds exactly what this frame would draw,
    // so collection, sort, SSBO writes and the render pass are all skipped. Bakes
    // never take it and invalidate at the end of the function.
    if (!probeCenter)
    {
      // First detected reason wins. The pending one goes first: a bake or a
      // shadows-off stretch invalidates without changing any digest.
      ShadowInvalidation reason = ShadowInvalidation::None;
      if (m_ShadowCachePendingReason != ShadowInvalidation::None)
        reason = m_ShadowCachePendingReason;
      else if (hasDirectionalShadow && m_ShadowManager.AnyRefitThisFit())
        reason = m_ShadowManager.LastRefitReasonThisFit();
      else if (shadowSettingsDigest != m_ShadowCachedSettingsDigest)
        reason = ShadowInvalidation::SettingsChanged;
      else if (frame.snapshot.casterIdentityDigest != m_ShadowCachedIdentityDigest)
        reason = ShadowInvalidation::CasterAddedOrRemoved;
      else if (frame.snapshot.casterTransformDigest != m_ShadowCachedTransformDigest)
        reason = ShadowInvalidation::CasterMoved;
      else if (frame.snapshot.lightDigest != m_ShadowCachedLightDigest
        || requestedSpotShadows != m_ShadowCachedSpotCount
        || requestedPointShadows != m_ShadowCachedPointCount)
        reason = ShadowInvalidation::LightParamsChanged;
      else if (shadowGeometryVersion != m_ShadowCachedGeometryVersion)
        reason = ShadowInvalidation::GeometryStreamedIn;

      if (b_ShadowAtlasContentValid && reason == ShadowInvalidation::None)
      {
        // SetUp above already refreshed this slot's UBOs: the matrices are
        // unchanged, but the other frame in flight keeps its own copy current.
        // Nothing writes the atlas on this path, so the frames-in-flight WAR
        // hazard the pass entry barrier guards against cannot arise either.
        return;
      }

      // The partial rebuild and the dirty rects share a precondition: the atlas
      // is valid, no pending reason, and every digest except the caster
      // transforms is equal. The reason chain above stops at the first hit, so
      // equality is re-checked wholesale here - a refit or a mover that
      // coincides with any OTHER digest change must stay a full rebuild.
      bool digestsEqualExceptTransform = shadowSettingsDigest == m_ShadowCachedSettingsDigest
        && frame.snapshot.casterIdentityDigest == m_ShadowCachedIdentityDigest
        && frame.snapshot.lightDigest == m_ShadowCachedLightDigest
        && requestedSpotShadows == m_ShadowCachedSpotCount
        && requestedPointShadows == m_ShadowCachedPointCount
        && shadowGeometryVersion == m_ShadowCachedGeometryVersion;
      bool transformDigestChanged =
        frame.snapshot.casterTransformDigest != m_ShadowCachedTransformDigest;

      if (b_ShadowAtlasContentValid && digestsEqualExceptTransform
        && m_ShadowCachePendingReason == ShadowInvalidation::None)
      {
        if (!transformDigestChanged
          && hasDirectionalShadow && m_ShadowManager.AnyRefitThisFit())
        {
          // The ONLY trigger is a cascade refit, so just the refitted cascade
          // tiles are redrawn. Spot and point tiles keep their content: their
          // matrices depend only on lights and casters, whose digests are equal
          // by precondition (a sun rotation refits all four cascades but still
          // leaves them alone).
          partialRebuild = true;
          std::fill(std::begin(spotTileDirty), std::end(spotTileDirty), false);
          for (auto& faces : pointFaceDirty)
            std::fill(std::begin(faces), std::end(faces), false);
          for (uint32_t i = 0; i < CSM_CASCADE_COUNT; i++)
            cascadeDirty[i] = m_ShadowManager.DidCascadeRefitThisFit(i);
        }
        else if (transformDigestChanged
          && !frame.snapshot.shadowMoverBounds.empty()
          && !frame.snapshot.shadowMoverUnbounded)
        {
          // Casters moved and the snapshot attributed every one of them a union
          // AABB (previous + current position). Each cascade tile gets the
          // projected footprint as a clear+redraw rect against its FROZEN
          // matrix; spot and point tiles are small (1024/512) and get whole-tile
          // granularity. A cascade refit that coincides simply makes that
          // cascade fully dirty.
          rectRebuild = true;

          const auto& movers = frame.snapshot.shadowMoverBounds;
          const auto& dirtyShadowData = m_ShadowManager.GetShadowData();

          if (hasDirectionalShadow)
          {
            for (uint32_t c = 0; c < CSM_CASCADE_COUNT; c++)
            {
              // A refitted cascade has a NEW matrix this frame: whole tile.
              if (m_ShadowManager.DidCascadeRefitThisFit(c))
                continue;

              auto sv = ShadowAtlas::GetCascadeViewport(c);
              const glm::mat4& tileViewProj = dirtyShadowData.cascades[c].viewProj;

              // Union of all mover rects in atlas pixels: one rect per tile.
              int32_t unionX0 = INT32_MAX, unionY0 = INT32_MAX;
              int32_t unionX1 = INT32_MIN, unionY1 = INT32_MIN;
              for (const auto& mover : movers)
              {
                // Orthographic projection: w is 1, so the NDC footprint of
                // the AABB is the min/max of its 8 projected corners, no
                // divide. XY only - a depth-clipped caster merely widens the
                // rect, which is harmless.
                glm::vec2 ndcMin(std::numeric_limits<float>::max());
                glm::vec2 ndcMax(std::numeric_limits<float>::lowest());
                for (uint32_t corner = 0; corner < 8; corner++)
                {
                  glm::vec4 clip = tileViewProj * glm::vec4(
                    (corner & 1) ? mover.max.x : mover.min.x,
                    (corner & 2) ? mover.max.y : mover.min.y,
                    (corner & 4) ? mover.max.z : mover.min.z,
                    1.0f);
                  ndcMin = glm::min(ndcMin, glm::vec2(clip));
                  ndcMax = glm::max(ndcMax, glm::vec2(clip));
                }
                ndcMin = glm::max(ndcMin, glm::vec2(-1.0f));
                ndcMax = glm::min(ndcMax, glm::vec2(1.0f));
                if (ndcMin.x >= ndcMax.x || ndcMin.y >= ndcMax.y)
                  continue; // this mover does not touch this tile

                // Tile NDC to atlas pixels via the viewport transform,
                // rounded outward.
                unionX0 = std::min(unionX0, int32_t(std::floor(
                  sv.viewport.x + (ndcMin.x * 0.5f + 0.5f) * sv.viewport.width)));
                unionY0 = std::min(unionY0, int32_t(std::floor(
                  sv.viewport.y + (ndcMin.y * 0.5f + 0.5f) * sv.viewport.height)));
                unionX1 = std::max(unionX1, int32_t(std::ceil(
                  sv.viewport.x + (ndcMax.x * 0.5f + 0.5f) * sv.viewport.width)));
                unionY1 = std::max(unionY1, int32_t(std::ceil(
                  sv.viewport.y + (ndcMax.y * 0.5f + 0.5f) * sv.viewport.height)));
              }

              if (unionX0 >= unionX1 || unionY0 >= unionY1)
              {
                cascadeDirty[c] = false;
                continue;
              }

              int32_t tileX0 = sv.scissor.offset.x;
              int32_t tileY0 = sv.scissor.offset.y;
              int32_t tileX1 = tileX0 + int32_t(sv.scissor.extent.width);
              int32_t tileY1 = tileY0 + int32_t(sv.scissor.extent.height);
              unionX0 = std::max(unionX0 - SHADOW_RECT_PAD_TEXELS, tileX0);
              unionY0 = std::max(unionY0 - SHADOW_RECT_PAD_TEXELS, tileY0);
              unionX1 = std::min(unionX1 + SHADOW_RECT_PAD_TEXELS, tileX1);
              unionY1 = std::min(unionY1 + SHADOW_RECT_PAD_TEXELS, tileY1);

              uint64_t rectArea = uint64_t(unionX1 - unionX0) * uint64_t(unionY1 - unionY0);
              uint64_t tileArea = uint64_t(sv.scissor.extent.width) * sv.scissor.extent.height;
              if (float(rectArea) > SHADOW_RECT_MAX_TILE_FRACTION * float(tileArea))
                continue; // whole tile is cheaper; cascadeDirty stays true, no rect

              cascadeRects[c] = CascadeDirtyRect {
                .hasRect = true,
                .rect = {
                  { unionX0, unionY0 },
                  { uint32_t(unionX1 - unionX0), uint32_t(unionY1 - unionY0) } },
                // The final padded rect mapped back to tile NDC, so the
                // crop-cull matches the scissor exactly.
                .ndcMin = {
                  (float(unionX0) - sv.viewport.x) / sv.viewport.width * 2.0f - 1.0f,
                  (float(unionY0) - sv.viewport.y) / sv.viewport.height * 2.0f - 1.0f },
                .ndcMax = {
                  (float(unionX1) - sv.viewport.x) / sv.viewport.width * 2.0f - 1.0f,
                  (float(unionY1) - sv.viewport.y) / sv.viewport.height * 2.0f - 1.0f },
              };
            }
          }

          // A spot/point-face tile is redrawn fully iff a mover's union AABB
          // intersects its frustum, else its content is exact and untouched.
          for (uint32_t i = 0; i < requestedSpotShadows; i++)
          {
            FrustumPlane spotPlanes[6];
            ExtractFrustumPlanes(dirtyShadowData.spotShadows[i].viewProj, spotPlanes);
            spotTileDirty[i] = false;
            for (const auto& mover : movers)
            {
              if (IsAABBVisible(mover.min, mover.max, spotPlanes, 6))
              {
                spotTileDirty[i] = true;
                break;
              }
            }
          }
          for (uint32_t i = 0; i < requestedPointShadows; i++)
          {
            for (uint32_t face = 0; face < 6; face++)
            {
              FrustumPlane facePlanes[6];
              ExtractFrustumPlanes(dirtyShadowData.pointShadows[i].faceViewProj[face], facePlanes);
              pointFaceDirty[i][face] = false;
              for (const auto& mover : movers)
              {
                if (IsAABBVisible(mover.min, mover.max, facePlanes, 6))
                {
                  pointFaceDirty[i][face] = true;
                  break;
                }
              }
            }
          }
        }
      }
    }

    // This render becomes the new cache baseline. Shared between the normal
    // tail below and the zero-tile rect early-out, so both write exactly the
    // values the next frame's decision compares.
    auto refreshCacheBaseline = [&]()
    {
      m_ShadowCachedIdentityDigest = frame.snapshot.casterIdentityDigest;
      m_ShadowCachedTransformDigest = frame.snapshot.casterTransformDigest;
      m_ShadowCachedLightDigest = frame.snapshot.lightDigest;
      m_ShadowCachedSettingsDigest = shadowSettingsDigest;
      m_ShadowCachedGeometryVersion = shadowGeometryVersion;
      m_ShadowCachedSpotCount = requestedSpotShadows;
      m_ShadowCachedPointCount = requestedPointShadows;
      b_ShadowAtlasContentValid = true;
      m_ShadowCachePendingReason = ShadowInvalidation::None;
    };

    if (rectRebuild)
    {
      bool anyTileDirty = false;
      if (hasDirectionalShadow)
      {
        for (uint32_t i = 0; i < CSM_CASCADE_COUNT; i++)
          anyTileDirty |= cascadeDirty[i];
      }
      for (uint32_t i = 0; i < requestedSpotShadows; i++)
        anyTileDirty |= spotTileDirty[i];
      for (uint32_t i = 0; i < requestedPointShadows; i++)
      {
        for (uint32_t face = 0; face < 6; face++)
          anyTileDirty |= pointFaceDirty[i][face];
      }

      // Every mover is beyond every cascade and light (the car drove past
      // shadowDistance): nothing to patch, but the transform digest DID
      // change, so the baseline must refresh through the rect outcome - a
      // plain HIT would leave the stale digest re-triggering forever. No
      // render pass begins; this is a skip that updates the digests.
      if (!anyTileDirty)
      {
        refreshCacheBaseline();
        return;
      }
    }

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
    // Only three fields decide the order, so those are what gets sorted: moving a
    // whole DrawCommand log n times is over a hundred bytes of copy per swap.
    // The command index breaks ties, which the comparators alone did not: an
    // unspecified order among identical keys could put a caster on a different side
    // of the model SSBO budget between a rebuild and the rect frames patching it.
    m_ShadowSortEntries.clear();
    m_ShadowSortEntries.reserve(m_ShadowDrawCommands.size());
    for (uint32_t i = 0; i < uint32_t(m_ShadowDrawCommands.size()); i++)
    {
      const DrawCommand& dc = m_ShadowDrawCommands[i];
      m_ShadowSortEntries.push_back({
        .key = useIndirect ? dc.ShadowBatchKey() : dc.SortKey(),
        .materialIndex = dc.materialIndex,
        .meshIndex = dc.meshIndex,
        .commandIndex = i,
      });
    }

    std::sort(m_ShadowSortEntries.begin(), m_ShadowSortEntries.end(),
      [](const ShadowSortEntry& a, const ShadowSortEntry& b)
      {
        if (a.key != b.key) return a.key < b.key;
        if (a.materialIndex != b.materialIndex) return a.materialIndex < b.materialIndex;
        if (a.meshIndex != b.meshIndex) return a.meshIndex < b.meshIndex;
        return a.commandIndex < b.commandIndex;
      });

    // Applying the permutation touches every command exactly once. The bounds are
    // split off into their own array here as well: the per-tile frustum cull below
    // runs once per drawn atlas tile over this whole list and reads nothing else,
    // so it has no reason to walk the full command stride.
    m_ShadowSortedCommands.clear();
    m_ShadowSortedCommands.reserve(m_ShadowDrawCommands.size());
    m_ShadowBounds.clear();
    m_ShadowBounds.reserve(m_ShadowDrawCommands.size());
    for (const auto& entry : m_ShadowSortEntries)
    {
      const DrawCommand& dc = m_ShadowDrawCommands[entry.commandIndex];
      m_ShadowBounds.push_back({ .min = dc.boundsMin, .max = dc.boundsMax });
      m_ShadowSortedCommands.push_back(dc);
    }
    m_ShadowDrawCommands.swap(m_ShadowSortedCommands);

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

    auto& meshManager = frame.assets.Meshes();

    // Ranges of m_ShadowDrawCommands that share a cull mode, one indirect draw each.
    size_t batchBegin[2] = { 0, 0 };
    size_t batchEnd[2] = { 0, 0 };
    // The free area of the indirect buffer is filled from both ends: 16-bit index
    // commands grow the head, 32-bit ones grow the tail downwards. One pass over the
    // casters then yields two contiguous ranges, one per index buffer bind, without
    // running the frustum test twice.
    uint32_t indirectCursor = 0;
    uint32_t indirectTail = 0;
    uint32_t indirectCapacity = 0;
    uint32_t modelCapacity = 0;
    VkBuffer indirectBuffer = VK_NULL_HANDLE;
    VkDrawIndexedIndirectCommand* indirectCommands = nullptr;

    // The model SSBO is cut into one equally sized block per atlas tile, laid out in
    // the order the tiles are drawn below. A tile writes viewProj * world into its own
    // block, so the matrix the shader loads is already a clip-space transform.
    glm::mat4* modelMatrices = nullptr;
    uint32_t modelTileStride = 0;
    uint32_t modelTileIndex = 0;

    // Only tiles actually drawn this frame: the model SSBO blocks and the
    // indirect commands are cut per DRAWN tile - drawShadowPass advances
    // modelTileIndex per invocation - so a partial frame sizes for its dirty
    // subset and the block addressing needs no other change.
    uint32_t tileCount = 0;
    if (hasDirectionalShadow)
    {
      for (uint32_t i = 0; i < CSM_CASCADE_COUNT; i++)
      {
        if (cascadeDirty[i])
          tileCount++;
      }
    }
    for (uint32_t i = 0; i < requestedSpotShadows; i++)
    {
      if (spotTileDirty[i])
        tileCount++;
    }
    for (uint32_t i = 0; i < requestedPointShadows; i++)
    {
      for (uint32_t face = 0; face < 6; face++)
      {
        if (pointFaceDirty[i][face])
          tileCount++;
      }
    }

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

        // A mesh the shadow position buffer had no room for simply keeps the legacy
        // per-draw path, which reads the exact stream and needs no restoring.
        if (useQuantizedPositions && !alloc.shadowPositionsResident)
          continue;

        auto& rec = m_ShadowIndirectRecords[i];
        rec.batchable = true;
        rec.vertexOffset = int32_t(useQuantizedPositions ? alloc.shadowVertexOffset : alloc.vertexOffset);
        rec.indexType = alloc.indexType;
        rec.dequantScale = alloc.dequantScale;
        rec.dequantBias = alloc.dequantBias;
        // GetLodRange already collapses missing levels onto the nearest populated
        // one, so the per-tile emission below never has to test for a level.
        for (uint32_t lod = 0; lod < MeshSimplifier::LOD_COUNT; lod++)
          rec.lods[lod] = vb.GetLodRange(lod);
        rec.instanceCount = dc.instanced ? uint32_t(dc.instanceData->size()) : 1u;
        requiredInstances += rec.instanceCount;
        batchableCount++;
      }

      // Every tile needs its own copy of the matrices, because each one folds in a
      // different projection.
      // Rewritten only when the handle actually changed: the set is still bound by the
      // command buffers of the other slots and there is no reason to touch it otherwise.
      if (GrowMappedSlot(ctx, m_ShadowModelBuffers[shadowSlot],
        requiredInstances * tileCount * sizeof(glm::mat4), SHADOW_MODEL_CAP_BYTES,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "model", b_ShadowModelOverflowReported))
      {
        m_ShadowModelDescriptorSets[shadowSlot].WriteStorageBuffer(0,
          m_ShadowModelBuffers[shadowSlot].Get(), m_ShadowModelBuffers[shadowSlot].GetSize());
      }
      modelCapacity = uint32_t(m_ShadowModelBuffers[shadowSlot].GetSize() / sizeof(glm::mat4));
      // Tiles get an equal share of the buffer. Casters past that share drop to the
      // legacy path, so every block ends up the same length and the stride is exact.
      // The share is cut against the atlas tile count at full capacity, NOT against
      // the number of tiles that happen to be dirty: a rect frame patches over a full
      // rebuild, and a caster that flipped between the two paths would redraw the same
      // geometry from the other position stream - quantized instead of exact - which
      // breaks the bit-identical-depth assumption the patch rests on.
      // tileCount <= MAX_SHADOW_TILES, so tileCount * tileBudget still fits.
      constexpr uint32_t tileBudget =
        uint32_t(SHADOW_MODEL_CAP_BYTES / sizeof(glm::mat4)) / MAX_SHADOW_TILES;

      // Pass two lays the final world matrices out in sorted order, in plain cached
      // memory: the per-tile products below read them back, which the write-combined
      // SSBO mapping would make ruinously slow. Instanced casters are premultiplied
      // here because the indirect shader reads one matrix and cannot do the
      // pc.world * instance product the legacy shader did.
      m_ShadowModelWorlds.resize(std::min<uint64_t>(requiredInstances, tileBudget));
      glm::mat4* models = m_ShadowModelWorlds.data();
      uint32_t modelCursor = 0;
      for (size_t i = 0; i < commandCount; i++)
      {
        auto& rec = m_ShadowIndirectRecords[i];
        if (!rec.batchable)
          continue;

        if (modelCursor + rec.instanceCount > tileBudget)
        {
          rec.batchable = false;
          batchableCount--;
          continue;
        }

        auto& dc = m_ShadowDrawCommands[i];
        rec.modelBase = modelCursor;

        // Restoring happens in mesh local space, so it multiplies in on the right of
        // everything the instance already carries.
        if (dc.instanced)
        {
          const auto& instances = *dc.instanceData;
          if (useQuantizedPositions)
          {
            for (uint32_t n = 0; n < rec.instanceCount; n++)
            {
              models[modelCursor + n] = FoldDequantization(dc.worldTransform * instances[n],
                rec.dequantScale, rec.dequantBias);
            }
          }
          else
          {
            for (uint32_t n = 0; n < rec.instanceCount; n++)
              models[modelCursor + n] = dc.worldTransform * instances[n];
          }
        }
        else
        {
          models[modelCursor] = useQuantizedPositions
            ? FoldDequantization(dc.worldTransform, rec.dequantScale, rec.dequantBias)
            : dc.worldTransform;
        }

        modelCursor += rec.instanceCount;
      }

      // Blocks are as long as the part of the share the casters actually used.
      modelTileStride = modelCursor;
      modelMatrices = static_cast<glm::mat4*>(m_ShadowModelBuffers[shadowSlot].GetMapped());

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

      GrowMappedSlot(ctx, m_ShadowIndirectBuffers[shadowSlot],
        VkDeviceSize(tileCount) * batchableCount * sizeof(VkDrawIndexedIndirectCommand),
        SHADOW_INDIRECT_CAP_BYTES, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, "indirect",
        b_ShadowIndirectOverflowReported);
      indirectBuffer = m_ShadowIndirectBuffers[shadowSlot].Get();
      indirectCommands = static_cast<VkDrawIndexedIndirectCommand*>(
        m_ShadowIndirectBuffers[shadowSlot].GetMapped());
      indirectCapacity = uint32_t(m_ShadowIndirectBuffers[shadowSlot].GetSize()
        / sizeof(VkDrawIndexedIndirectCommand));
      indirectTail = indirectCapacity;
    }

    auto& atlas = m_ShadowManager.GetAtlas();

    // Upload instance data once before all shadow passes. A host write into a
    // mapped buffer, but kept above the pass begin so nothing besides draws and
    // clears sits inside the render pass instance.
    for (auto& dc : m_ShadowDrawCommands)
    {
      if (dc.instanced)
      {
        m_InstanceBuffer.Update(dc.instanceOffset, dc.instanceData->data(),
          uint32_t(dc.instanceData->size() * sizeof(glm::mat4)));
      }
    }

    VkClearValue clearValue {};
    clearValue.depthStencil = { 1.0f, 0 };

    // One render pass instance over the whole atlas. A bake clears it outright;
    // every other frame loads it and clears only the tiles it redraws, because
    // untouched tiles must keep their cached content.
    {
      VkRenderPassBeginInfo rpBegin {};
      rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      rpBegin.renderPass = isBake ? atlas.GetRenderPass() : atlas.GetLoadRenderPass();
      rpBegin.framebuffer = atlas.GetFramebuffer();
      rpBegin.renderArea = { {0, 0}, atlas.GetExtent() };
      rpBegin.clearValueCount = 1;
      rpBegin.pClearValues = &clearValue;
      DebugMarker::BeginLabel(cmd, "Shadows");
      vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdSetDepthBias(cmd, 0.5f, 0.0f, 1.5f);
    }

    // Helper lambda: draw all shadow commands through the given tile projection.
    // frustumPlanes + planeCount for frustum culling; nullptr disables frustum culling.
    // dirtyRect (atlas pixels, pre-clamped to the tile) narrows the clear and
    // the scissor to a mover footprint; nullptr keeps the whole tile.
    auto drawShadowPass = [&](const glm::mat4& viewProj, const ShadowViewport& sv,
      const FrustumPlane* frustumPlanes = nullptr, int planeCount = 6,
      uint32_t lodLevel = 0, const VkRect2D* dirtyRect = nullptr)
    {
      if (!isBake)
      {
        VkClearAttachment clearAttachment {};
        clearAttachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        clearAttachment.clearValue = clearValue;

        VkClearRect clearRect {};
        clearRect.rect = dirtyRect ? *dirtyRect : sv.scissor;
        clearRect.baseArrayLayer = 0;
        clearRect.layerCount = 1;
        vkCmdClearAttachments(cmd, 1, &clearAttachment, 1, &clearRect);
      }

      vkCmdSetViewport(cmd, 0, 1, &sv.viewport);
      // A rect update shrinks only the scissor. The viewport - and with it
      // the projection mapping - and the tile's frozen matrix stay untouched,
      // so redrawing static geometry inside the rect with the same matrix,
      // same pipeline, same LOD and same depth bias is deterministic:
      // overlapping texels reproduce bit-identical depth.
      VkRect2D tileScissor = dirtyRect ? *dirtyRect : sv.scissor;
      vkCmdSetScissor(cmd, 0, 1, &tileScissor);

      // Tiles are visited in the order the buffer was cut for, so the counter alone
      // picks this tile's block.
      uint32_t modelTileBase = modelTileIndex++ * modelTileStride;

      // The blocks were sized for exactly tileCount tiles, and a tile past that would
      // write outside the mapping.
      if (useIndirect && modelTileBase + modelTileStride <= modelCapacity)
      {
        // One contiguous range of commands, drawn against the index buffer it was
        // built for. Split across several submissions when the device caps how many
        // commands one call may carry.
        auto issueRange = [&](uint32_t first, uint32_t count, VkIndexType indexType)
        {
          if (count == 0)
            return;

          vkCmdBindIndexBuffer(cmd, ctx.geometryArena->GetIndexBuffer(indexType), 0, indexType);

          uint32_t maxPerCall = std::max(ctx.maxDrawIndirectCount, 1u);
          if (count > maxPerCall && !b_ShadowIndirectCountSplitReported)
          {
            b_ShadowIndirectCountSplitReported = true;
            YA_LOG_WARN("Render",
              "Shadow indirect range of %u commands exceeds maxDrawIndirectCount %u, splitting",
              count, maxPerCall);
          }

          for (uint32_t issued = 0; issued < count; issued += maxPerCall)
          {
            uint32_t batch = std::min(maxPerCall, count - issued);
            vkCmdDrawIndexedIndirect(cmd, indirectBuffer,
              VkDeviceSize(first + issued) * sizeof(VkDrawIndexedIndirectCommand),
              batch, sizeof(VkDrawIndexedIndirectCommand));
          }
        };

        for (uint32_t cullMode = 0; cullMode < 2; cullMode++)
        {
          uint32_t narrowFirst = indirectCursor;
          uint32_t wideEnd = indirectTail;

          for (size_t i = batchBegin[cullMode]; i < batchEnd[cullMode]; i++)
          {
            auto& rec = m_ShadowIndirectRecords[i];
            if (!rec.batchable)
              continue;

            // Same test, same planes, same plane count as the legacy loop below:
            // the cascades pass 5 planes on purpose and passing 6 here would clip
            // distant casters out of them.
            const ShadowBounds& bounds = m_ShadowBounds[i];
            bool hasBounds = bounds.min.x <= bounds.max.x;
            if (hasBounds && frustumPlanes)
            {
              if (!IsAABBVisible(bounds.min, bounds.max, frustumPlanes, planeCount))
                continue;
            }

            // The two ends meeting is what full means now, not reaching the capacity.
            if (indirectCursor >= indirectTail)
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

            // The tile projection is folded in here instead of being pushed, so the
            // vertex shader is one matrix load and one product. Only casters that
            // survived this tile's cull are written, and the block fills in ascending
            // order because the records are visited in the order they were laid out.
            uint32_t modelSlot = modelTileBase + rec.modelBase;
            for (uint32_t n = 0; n < rec.instanceCount; n++)
              modelMatrices[modelSlot + n] = viewProj * m_ShadowModelWorlds[rec.modelBase + n];

            const MeshLodRange& lod = rec.lods[lodLevel];

            bool narrow = rec.indexType == VK_INDEX_TYPE_UINT16;
            VkDrawIndexedIndirectCommand& out = indirectCommands[narrow ? indirectCursor++ : --indirectTail];
            out.indexCount = lod.indexCount;
            out.instanceCount = rec.instanceCount;
            out.firstIndex = lod.firstIndex;
            out.vertexOffset = rec.vertexOffset;
            // Picks the batch's first matrix inside this tile's block; gl_InstanceIndex
            // adds the rest.
            out.firstInstance = modelSlot;
          }

          uint32_t narrowCount = indirectCursor - narrowFirst;
          uint32_t wideCount = wideEnd - indirectTail;
          if (narrowCount == 0 && wideCount == 0)
            continue;

          auto& pipeline = m_PSOCache.Get(useQuantizedPositions
            ? m_ShadowIndirectQuantizedPipelines[cullMode]
            : m_ShadowIndirectPipelines[cullMode]);
          pipeline.Bind(cmd);
          // Set 0 is the cascade UBO the shaders stopped reading when the tile
          // projection moved into the model matrix. It is still in the pipeline
          // layout, but Vulkan only requires a set to be bound when it is
          // statically accessed, so nothing binds it any more.
          pipeline.BindDescriptorSets(cmd,
            { m_ShadowModelDescriptorSets[shadowSlot].Get() }, 1);

          // Re-bound every tile because the alpha-test tail below binds its own
          // interleaved vertex stream over these.
          VkBuffer positions = useQuantizedPositions
            ? ctx.geometryArena->GetShadowPositionBuffer()
            : ctx.geometryArena->GetPositionBuffer();
          VkDeviceSize positionOffset = 0;
          vkCmdBindVertexBuffers(cmd, 0, 1, &positions, &positionOffset);

          issueRange(narrowFirst, narrowCount, VK_INDEX_TYPE_UINT16);
          issueRange(indirectTail, wideCount, VK_INDEX_TYPE_UINT32);
        }
      }

      uint8_t lastSortKey = UINT8_MAX;
      uint32_t lastMaterialIndex = UINT32_MAX;
      uint32_t lastMaterialGen = UINT32_MAX;
      VulkanPipeline* currentPipeline = nullptr;
      // Scoped to the tile lambda, so it starts empty for every atlas tile and
      // cannot outlive the command buffer recording it.
      MeshBindCache bindCache;

      // On the indirect path this is the alpha-test tail plus anything the arena could
      // not take; otherwise it is the whole pass, unchanged.
      size_t legacyCount = useIndirect ? m_ShadowLegacyIndices.size() : m_ShadowDrawCommands.size();
      for (size_t legacyIndex = 0; legacyIndex < legacyCount; legacyIndex++)
      {
        size_t commandIndex = useIndirect ? m_ShadowLegacyIndices[legacyIndex] : legacyIndex;
        // Culled casters never touch the command itself, which is why the bounds
        // live in their own array.
        const ShadowBounds& bounds = m_ShadowBounds[commandIndex];
        bool hasBounds = bounds.min.x <= bounds.max.x;
        if (hasBounds && frustumPlanes)
        {
          if (!IsAABBVisible(bounds.min, bounds.max, frustumPlanes, planeCount))
            continue;
        }

        auto& dc = m_ShadowDrawCommands[commandIndex];
        MeshHandle meshHandle { dc.meshIndex, dc.meshGeneration };
        uint8_t sortKey = dc.SortKey();

        if (sortKey != lastSortKey)
        {
          currentPipeline = &m_PSOCache.Get(m_ShadowPipelines[sortKey]);
          currentPipeline->Bind(cmd);
          // See the indirect branch above: set 0 is dead in every shadow shader.
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
        if (dc.isAlphaTest)
        {
          // Alpha-test casters draw the interleaved stream for their UVs, which has
          // no simplified counterpart: LOD lives on the position-only stream.
          vb.Draw(cmd, instanceCount, &bindCache);
        }
        else
        {
          vb.DrawPositionOnly(cmd, instanceCount, lodLevel, &bindCache);
        }
      }
    };

    auto& shadowData = m_ShadowManager.GetShadowData();

    // Every tile gets its own label so a GPU profiler can price cascades and lights
    // one by one. DebugMarker is already a no-op outside debug builds.
    char tileLabel[32];

#ifdef YA_EDITOR
    // Bakes record on a single-time command buffer with no query pool for this
    // frame, so their per-tile zones go through GpuZoneScope's null-profiler path.
    GpuProfiler* tileProfiler = probeCenter ? nullptr : &m_GpuProfiler;
#endif

    // CSM cascades - frustum cull with 5 planes (skip near plane to keep distant shadow casters)
    if (hasDirectionalShadow)
    {
      DebugMarker::BeginLabel(cmd, "CSM");
      for (uint32_t cascade = 0; cascade < CSM_CASCADE_COUNT; cascade++)
      {
        // Partial frames draw only the refitted cascades; the frozen matrices
        // of the others are bit-identical, so the atlas already matches them.
        // Skipped tiles get no GPU zone and no label this frame.
        if (!cascadeDirty[cascade])
          continue;

        // Rect tiles cull against the crop product: the rect's NDC sub-range
        // remapped to full NDC in front of the frozen viewProj. Purely a
        // CPU/GPU submission win - correctness comes from the scissor. Only
        // left/right/top/bottom tighten; the near/far rows are untouched, so
        // the 5-plane skip-near convention below is preserved exactly.
        const VkRect2D* tileRect = nullptr;
        glm::mat4 cullViewProj = shadowData.cascades[cascade].viewProj;
        if (rectRebuild && cascadeRects[cascade].hasRect)
        {
          tileRect = &cascadeRects[cascade].rect;
          cullViewProj = MakeNdcCropMatrix(cascadeRects[cascade].ndcMin,
            cascadeRects[cascade].ndcMax) * cullViewProj;
        }

        FrustumPlane allPlanes[6];
        ExtractFrustumPlanes(cullViewProj, allPlanes);

        // Skip near plane (index 4): left, right, bottom, top, far
        FrustumPlane csmPlanes[5] = { allPlanes[0], allPlanes[1], allPlanes[2], allPlanes[3], allPlanes[5] };

        auto sv = ShadowAtlas::GetCascadeViewport(cascade);

        uint32_t lodLevel = 0;
        if (b_ShadowLodEnabled)
        {
          lodLevel = uint32_t(std::clamp(m_ShadowCascadeLods[cascade], 0,
            int(MeshSimplifier::LOD_COUNT) - 1));
        }

        snprintf(tileLabel, sizeof(tileLabel), "Cascade %u", cascade);
        DebugMarker::BeginLabel(cmd, tileLabel);
        {
#ifdef YA_EDITOR
          GpuZoneScope tileZone(tileProfiler, cmd, tileLabel);
#endif
          drawShadowPass(shadowData.cascades[cascade].viewProj, sv,
            csmPlanes, 5, lodLevel, tileRect);
        }
        DebugMarker::EndLabel(cmd);
      }
      DebugMarker::EndLabel(cmd);
    }

    // Spot shadows - full 6-plane frustum culling. Partial frames skip them
    // wholesale, rect frames redraw exactly the tiles a mover intersects:
    // their matrices depend only on lights and casters, whose digests were
    // equal, so an untouched tile's atlas content is still exact.
    bool anySpotTiles = false;
    for (uint32_t i = 0; i < requestedSpotShadows; i++)
      anySpotTiles |= spotTileDirty[i];
    if (hasSpotShadows && anySpotTiles)
      DebugMarker::BeginLabel(cmd, "Spot Shadows");
    for (uint32_t i = 0; i < requestedSpotShadows; i++)
    {
      if (!spotTileDirty[i])
        continue;

      FrustumPlane spotPlanes[6];
      ExtractFrustumPlanes(shadowData.spotShadows[i].viewProj, spotPlanes);

      auto sv = ShadowAtlas::GetSpotViewport(i);

      snprintf(tileLabel, sizeof(tileLabel), "Spot %u", i);
      DebugMarker::BeginLabel(cmd, tileLabel);
      {
#ifdef YA_EDITOR
        GpuZoneScope tileZone(tileProfiler, cmd, tileLabel);
#endif
        drawShadowPass(shadowData.spotShadows[i].viewProj, sv, spotPlanes, 6);
      }
      DebugMarker::EndLabel(cmd);
    }
    if (hasSpotShadows && anySpotTiles)
      DebugMarker::EndLabel(cmd);

    // Point shadows - per-face frustum culling
    bool anyPointTiles = false;
    for (uint32_t i = 0; i < requestedPointShadows; i++)
    {
      for (uint32_t face = 0; face < 6; face++)
        anyPointTiles |= pointFaceDirty[i][face];
    }
    if (hasPointShadows && anyPointTiles)
      DebugMarker::BeginLabel(cmd, "Point Shadows");
    for (uint32_t i = 0; i < requestedPointShadows; i++)
    {
      bool anyFaceDirty = false;
      for (uint32_t face = 0; face < 6; face++)
        anyFaceDirty |= pointFaceDirty[i][face];
      if (!anyFaceDirty)
        continue;
#ifdef YA_EDITOR
      // One zone per point light: its six faces share a budget, and per-face
      // timings would only add noise while eating query pool slots.
      snprintf(tileLabel, sizeof(tileLabel), "Point %u", i);
      GpuZoneScope pointZone(tileProfiler, cmd, tileLabel);
#endif
      for (uint32_t face = 0; face < 6; face++)
      {
        if (!pointFaceDirty[i][face])
          continue;

        FrustumPlane facePlanes[6];
        ExtractFrustumPlanes(shadowData.pointShadows[i].faceViewProj[face], facePlanes);

        auto sv = ShadowAtlas::GetPointFaceViewport(i, face);

        snprintf(tileLabel, sizeof(tileLabel), "Point %u Face %u", i, face);
        DebugMarker::BeginLabel(cmd, tileLabel);
        drawShadowPass(shadowData.pointShadows[i].faceViewProj[face], sv, facePlanes, 6);
        DebugMarker::EndLabel(cmd);
      }
    }
    if (hasPointShadows && anyPointTiles)
      DebugMarker::EndLabel(cmd);

    vkCmdEndRenderPass(cmd);
    DebugMarker::EndLabel(cmd);

    if (probeCenter)
    {
      // The bake overwrote the whole atlas with matrices fitted around the
      // probe, so the next camera frame must redraw everything it cached.
      b_ShadowAtlasContentValid = false;
      m_ShadowCachePendingReason = ShadowInvalidation::ProbeBake;
    }
    else
    {
      // This render is the new cache baseline.
      refreshCacheBaseline();
    }
  }

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
