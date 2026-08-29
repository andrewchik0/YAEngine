#include "Render.h"
#include "BloomData.h"
#include "ExposureData.h"

namespace YAEngine
{
  void Render::WriteIrradianceVolumeDescriptors()
  {
    for (size_t i = 0; i < m_IBLDescriptorSets.size(); i++)
    {
      for (uint32_t channel = 0; channel < 3; channel++)
      {
        m_IBLDescriptorSets[i].WriteCombinedImageSampler(5 + channel,
          m_VolumeStorage.GetCoefficientView(channel),
          m_VolumeStorage.GetCoefficientSampler(channel));
      }
      m_IBLDescriptorSets[i].WriteCombinedImageSampler(8,
        m_VolumeStorage.GetValidityView(), m_VolumeStorage.GetValiditySampler());
      m_IBLDescriptorSets[i].WriteUniformBuffer(9,
        m_VolumeStorage.GetBuffer(uint32_t(i)), sizeof(IrradianceVolumeBuffer));
    }
  }

  VulkanPipeline& Render::GetForwardPipeline(const DrawCommand& dc)
  {
    return m_PSOCache.Get(m_ForwardPipelines[dc.SortKey()]);
  }

  VulkanPipeline& Render::GetForwardTransparentPipeline(const DrawCommand& dc)
  {
    uint32_t idx = (dc.instanced ? 2u : 0u) + (dc.doubleSided ? 1u : 0u);
    return m_PSOCache.Get(m_ForwardTransparentPipelines[idx]);
  }

  VulkanPipeline& Render::GetWireframePipeline(const DrawCommand& dc)
  {
    return m_PSOCache.Get(m_WireframePipelines[dc.SortKey()]);
  }

  VulkanPipeline& Render::GetWireframeTransparentPipeline(const DrawCommand& dc)
  {
    uint32_t idx = (dc.instanced ? 2u : 0u) + (dc.doubleSided ? 1u : 0u);
    return m_PSOCache.Get(m_WireframeTransparentPipelines[idx]);
  }

#ifdef YA_EDITOR
  VulkanPipeline& Render::GetPickPipeline(const DrawCommand& dc)
  {
    // Cull mode must match the original draw, or a back face visible on screen leaves a hole in the id buffer (alpha-test and unlit are always rendered double-sided).
    uint32_t idx;
    if (dc.isAlphaTest)
      idx = dc.instanced ? 5 : 4;
    else
      idx = (dc.instanced ? 2 : 0) + ((dc.doubleSided || dc.noShading) ? 1 : 0);

    return m_PSOCache.Get(m_PickPipelines[idx]);
  }
#endif

  VulkanPipeline& Render::GetDepthPipeline(const DrawCommand& dc)
  {
    assert(!dc.noShading && "Depth pipeline not available for noShading draw commands");
    return m_PSOCache.Get(m_DepthPipelines[dc.SortKey()]);
  }

  void Render::CreateShadowIndirectResources()
  {
    auto& ctx = m_Backend.GetContext();

    // One extra slot past frames-in-flight: probe/irradiance volume bakes render the shadow atlas on a single-time command buffer outside the frame loop and must not write into a slot the frame loop still owns.
    uint32_t slotCount = ctx.maxFramesInFlight + 1;

    SetDescription modelDesc = {
      .set = 1,
      .bindings = {
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT }
      }
    };

    m_ShadowModelBuffers.resize(slotCount);
    m_ShadowModelDescriptorSets.resize(slotCount);
    m_ShadowIndirectBuffers.resize(slotCount);

    for (uint32_t i = 0; i < slotCount; i++)
    {
      m_ShadowModelBuffers[i] = VulkanBuffer::CreateMapped(ctx, SHADOW_MODEL_INITIAL_BYTES,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
      m_ShadowModelDescriptorSets[i].Init(ctx, modelDesc);
      m_ShadowModelDescriptorSets[i].WriteStorageBuffer(0, m_ShadowModelBuffers[i].Get(),
        m_ShadowModelBuffers[i].GetSize());

      m_ShadowIndirectBuffers[i] = VulkanBuffer::CreateMapped(ctx, SHADOW_INDIRECT_INITIAL_BYTES,
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
    }
  }

  void Render::DestroyShadowIndirectResources()
  {
    auto& ctx = m_Backend.GetContext();

    for (auto& set : m_ShadowModelDescriptorSets)
      set.Destroy();
    for (auto& buffer : m_ShadowModelBuffers)
      buffer.Destroy(ctx);
    for (auto& buffer : m_ShadowIndirectBuffers)
      buffer.Destroy(ctx);

    m_ShadowModelDescriptorSets.clear();
    m_ShadowModelBuffers.clear();
    m_ShadowIndirectBuffers.clear();
  }

  void Render::InitPipelines()
  {
    auto& ctx = m_Backend.GetContext();
    auto pipelineCache = ctx.pipelineCache;

    SetDescription instanceDesc = {
      .set = 3,
      .bindings = {
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT }
      }
    };
    m_InstanceDescriptorSet.Init(ctx, instanceDesc);
    m_InstanceBuffer.Create(ctx, MAX_INSTANCES * sizeof(glm::mat4));
    m_InstanceDescriptorSet.WriteStorageBuffer(0, m_InstanceBuffer.Get(), MAX_INSTANCES * sizeof(glm::mat4));

    constexpr VkDeviceSize prevWorldBytes = MAX_PREV_WORLD_MATRICES * sizeof(glm::mat4);
    SetDescription prevWorldDesc = {
      .set = 2,
      .bindings = {
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT }
      }
    };
    m_PrevWorldDescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    m_PrevWorldBuffers.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_PrevWorldBuffers.size(); i++)
    {
      m_PrevWorldDescriptorSets[i].Init(ctx, prevWorldDesc);
      m_PrevWorldBuffers[i].Create(ctx, prevWorldBytes);
      m_PrevWorldDescriptorSets[i].WriteStorageBuffer(0, m_PrevWorldBuffers[i].Get(), prevWorldBytes);
    }

    // Every full mesh.vert permutation reads the previous world matrix through this
    // set and indexes it through an extra push constant.
    const uint32_t gbufferPushConstantSize = sizeof(glm::mat4) + sizeof(int) + sizeof(uint32_t);

    VkRenderPass depthRP = m_Graph.GetPassRenderPass(m_DepthPrepassIndex);

    PipelineCreateInfo depthInfo = {
      .vertexShaderFile = "mesh_depth.vert",
      .pushConstantSize = sizeof(glm::mat4) + sizeof(int),
      .colorAttachmentCount = 0,
      .vertexInputFormat = "f3",
      .sets = std::vector({ m_FrameUniformBuffer.GetLayout() })
    };

    // [0] normal, [1] doubleSided
    m_DepthPipelines[0] = m_PSOCache.Register(ctx.device, depthRP, depthInfo, pipelineCache);
    depthInfo.doubleSided = true;
    m_DepthPipelines[1] = m_PSOCache.Register(ctx.device, depthRP, depthInfo, pipelineCache);

    // [2] instanced, [3] instanced+doubleSided
    depthInfo.doubleSided = false;
    depthInfo.vertexShaderFile = "mesh_instanced_depth.vert";
    depthInfo.sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_InstanceDescriptorSet.GetLayout() });
    m_DepthPipelines[2] = m_PSOCache.Register(ctx.device, depthRP, depthInfo, pipelineCache);
    depthInfo.doubleSided = true;
    m_DepthPipelines[3] = m_PSOCache.Register(ctx.device, depthRP, depthInfo, pipelineCache);

    // [6] alpha-test non-instanced, [7] alpha-test instanced
    // Alpha-test always rendered doubleSided (foliage).
    {
      PipelineCreateInfo depthAlphaInfo = {
        .fragmentShaderFile = "alphatest_discard.frag",
        .vertexShaderFile = "mesh_depth_alphatest.vert",
        .pushConstantSize = sizeof(glm::mat4) + sizeof(int),
        .doubleSided = true,
        .colorAttachmentCount = 0,
        .vertexInputFormat = "f3|f2f3f4",
        .sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_DefaultMaterial.GetLayout() })
      };
      m_DepthPipelines[6] = m_PSOCache.Register(ctx.device, depthRP, depthAlphaInfo, pipelineCache);

      depthAlphaInfo.vertexShaderFile = "mesh_instanced_depth_alphatest.vert";
      depthAlphaInfo.sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_DefaultMaterial.GetLayout(), m_InstanceDescriptorSet.GetLayout() });
      m_DepthPipelines[7] = m_PSOCache.Register(ctx.device, depthRP, depthAlphaInfo, pipelineCache);
    }

    // Shadow pipelines (depth-only with depth bias, using shadow atlas render pass)
    VkRenderPass shadowRP = m_ShadowManager.GetAtlas().GetRenderPass();
    {
      // Shadow maps stay standard-Z (atlas cleared to 1.0, LESS_OR_EQUAL sampler compare),
      // so they must not inherit the reversed-Z GREATER default.
      PipelineCreateInfo shadowInfo = {
        .vertexShaderFile = "shadow.vert",
        // viewProj * world folded into one matrix, plus the instance offset. Pushing
        // the tile projection next to the model matrix would need 132 bytes.
        .pushConstantSize = sizeof(glm::mat4) + sizeof(int),
        .colorAttachmentCount = 0,
        .compareOp = VK_COMPARE_OP_LESS,
        .vertexInputFormat = "f3",
        .sets = std::vector({ m_ShadowManager.GetShadowCascadeUBOLayout() })
      };
      shadowInfo.depthBiasEnable = true;
      // Casters above a cascade near plane are kept out of the CPU cull on purpose
      // (RenderShadowMaps skips that plane), so the rasterizer must not clip them either.
      shadowInfo.depthClampEnable = ctx.depthClampSupported;

      // [0] normal, [1] doubleSided
      m_ShadowPipelines[0] = m_PSOCache.Register(ctx.device, shadowRP, shadowInfo, pipelineCache);
      shadowInfo.doubleSided = true;
      m_ShadowPipelines[1] = m_PSOCache.Register(ctx.device, shadowRP, shadowInfo, pipelineCache);

      // [2] instanced, [3] instanced+doubleSided
      shadowInfo.doubleSided = false;
      shadowInfo.vertexShaderFile = "shadow_instanced.vert";
      shadowInfo.sets = std::vector({ m_ShadowManager.GetShadowCascadeUBOLayout(), m_InstanceDescriptorSet.GetLayout() });
      m_ShadowPipelines[2] = m_PSOCache.Register(ctx.device, shadowRP, shadowInfo, pipelineCache);
      shadowInfo.doubleSided = true;
      m_ShadowPipelines[3] = m_PSOCache.Register(ctx.device, shadowRP, shadowInfo, pipelineCache);

      // [6] alpha-test non-instanced, [7] alpha-test instanced
      // Alpha-test always rendered doubleSided (foliage).
      PipelineCreateInfo shadowAlphaInfo = {
        .fragmentShaderFile = "alphatest_discard.frag",
        .vertexShaderFile = "shadow_alphatest.vert",
        .pushConstantSize = sizeof(glm::mat4) + sizeof(int),
        .doubleSided = true,
        .colorAttachmentCount = 0,
        .compareOp = VK_COMPARE_OP_LESS,
        .vertexInputFormat = "f3|f2f3f4",
        .sets = std::vector({ m_ShadowManager.GetShadowCascadeUBOLayout(), m_DefaultMaterial.GetLayout() })
      };
      shadowAlphaInfo.depthBiasEnable = true;
      shadowAlphaInfo.depthClampEnable = ctx.depthClampSupported;
      m_ShadowPipelines[6] = m_PSOCache.Register(ctx.device, shadowRP, shadowAlphaInfo, pipelineCache);

      shadowAlphaInfo.vertexShaderFile = "shadow_instanced_alphatest.vert";
      shadowAlphaInfo.sets = std::vector({ m_ShadowManager.GetShadowCascadeUBOLayout(), m_DefaultMaterial.GetLayout(), m_InstanceDescriptorSet.GetLayout() });
      m_ShadowPipelines[7] = m_PSOCache.Register(ctx.device, shadowRP, shadowAlphaInfo, pipelineCache);
    }

    CreateShadowIndirectResources();

    // Indirect opaque shadow pipelines: same as above but the clip-space matrix comes from a shared SSBO addressed by gl_InstanceIndex instead of a push constant, so every depth-related state must stay identical or the two paths stop being comparable.
    {
      PipelineCreateInfo shadowIndirectInfo = {
        .vertexShaderFile = "shadow_indirect.vert",
        // The tile projection is premultiplied into the SSBO matrices, so this
        // variant has nothing left to push.
        .pushConstantSize = 0,
        .colorAttachmentCount = 0,
        .compareOp = VK_COMPARE_OP_LESS,
        .vertexInputFormat = "f3",
        .sets = std::vector({ m_ShadowManager.GetShadowCascadeUBOLayout(),
          m_ShadowModelDescriptorSets[0].GetLayout() })
      };
      shadowIndirectInfo.depthBiasEnable = true;
      shadowIndirectInfo.depthClampEnable = ctx.depthClampSupported;

      m_ShadowIndirectPipelines[0] = m_PSOCache.Register(ctx.device, shadowRP, shadowIndirectInfo, pipelineCache);
      shadowIndirectInfo.doubleSided = true;
      m_ShadowIndirectPipelines[1] = m_PSOCache.Register(ctx.device, shadowRP, shadowIndirectInfo, pipelineCache);

      // Quantized twin of the pair above: same shader file, restoring transform rides in the model matrix, so only the attribute format differs and depth-related state must stay identical for a fair comparison.
      if (ctx.unorm16VertexSupported)
      {
        shadowIndirectInfo.doubleSided = false;
        shadowIndirectInfo.vertexInputFormat = "n4";
        m_ShadowIndirectQuantizedPipelines[0] = m_PSOCache.Register(ctx.device, shadowRP, shadowIndirectInfo, pipelineCache);
        shadowIndirectInfo.doubleSided = true;
        m_ShadowIndirectQuantizedPipelines[1] = m_PSOCache.Register(ctx.device, shadowRP, shadowIndirectInfo, pipelineCache);
      }
    }

    VkRenderPass mainRP = m_Graph.GetPassRenderPass(m_GBufferPassIndex);

    PipelineCreateInfo forwardInfo = {
      .fragmentShaderFile = "gbuffer.frag",
      .vertexShaderFile = "mesh.vert",
      .pushConstantSize = gbufferPushConstantSize,
      .depthWrite = false,
      .colorAttachmentCount = 3,
      .compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
      .vertexInputFormat = "f3|f2f3f4",
      .sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_DefaultMaterial.GetLayout(),
        m_PrevWorldDescriptorSets[0].GetLayout() })
    };

    // [0] normal, [1] doubleSided
    m_ForwardPipelines[0] = m_PSOCache.Register(ctx.device, mainRP, forwardInfo, pipelineCache);
    forwardInfo.doubleSided = true;
    m_ForwardPipelines[1] = m_PSOCache.Register(ctx.device, mainRP, forwardInfo, pipelineCache);

    // [2] instanced, [3] instanced+doubleSided
    // The instance buffer claims set 2 here, so the previous world matrices move to set 3.
    forwardInfo.sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_DefaultMaterial.GetLayout(),
      m_InstanceDescriptorSet.GetLayout(), m_PrevWorldDescriptorSets[0].GetLayout() });
    forwardInfo.vertexShaderFile = "mesh_instanced.vert";
    forwardInfo.doubleSided = false;
    m_ForwardPipelines[2] = m_PSOCache.Register(ctx.device, mainRP, forwardInfo, pipelineCache);
    forwardInfo.doubleSided = true;
    m_ForwardPipelines[3] = m_PSOCache.Register(ctx.device, mainRP, forwardInfo, pipelineCache);

    // [4] noShading
    forwardInfo.sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_DefaultMaterial.GetLayout(),
      m_PrevWorldDescriptorSets[0].GetLayout() });
    forwardInfo.doubleSided = true;
    forwardInfo.fragmentShaderFile = "gbuffer_unlit.frag";
    forwardInfo.vertexShaderFile = "mesh.vert";
    m_ForwardPipelines[4] = m_PSOCache.Register(ctx.device, mainRP, forwardInfo, pipelineCache);

    // [5] terrain (two-layer splatting)
    {
      PipelineCreateInfo terrainInfo = {
        .fragmentShaderFile = "gbuffer_terrain.frag",
        .vertexShaderFile = "mesh.vert",
        .pushConstantSize = gbufferPushConstantSize,
        .depthWrite = false,
        .colorAttachmentCount = 3,
        .compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
        .vertexInputFormat = "f3|f2f3f4",
        .sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_TerrainMaterial.GetLayout(),
          m_PrevWorldDescriptorSets[0].GetLayout() })
      };
      m_ForwardPipelines[5] = m_PSOCache.Register(ctx.device, mainRP, terrainInfo, pipelineCache);
    }

    // [6] alpha-test non-instanced (depth from prepass - no depth write, GEQUAL)
    {
      PipelineCreateInfo alphaTestInfo = {
        .fragmentShaderFile = "gbuffer_alphatest.frag",
        .vertexShaderFile = "mesh.vert",
        .pushConstantSize = gbufferPushConstantSize,
        .depthWrite = false,
        .doubleSided = true,
        .colorAttachmentCount = 3,
        .compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
        .vertexInputFormat = "f3|f2f3f4",
        .sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_DefaultMaterial.GetLayout(),
          m_PrevWorldDescriptorSets[0].GetLayout() })
      };
      m_ForwardPipelines[6] = m_PSOCache.Register(ctx.device, mainRP, alphaTestInfo, pipelineCache);
    }

    // [7] alpha-test instanced (depth from prepass - no depth write, GEQUAL)
    {
      PipelineCreateInfo alphaTestInstInfo = {
        .fragmentShaderFile = "gbuffer_alphatest.frag",
        .vertexShaderFile = "mesh_instanced.vert",
        .pushConstantSize = gbufferPushConstantSize,
        .depthWrite = false,
        .doubleSided = true,
        .colorAttachmentCount = 3,
        .compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
        .vertexInputFormat = "f3|f2f3f4",
        .sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_DefaultMaterial.GetLayout(),
          m_InstanceDescriptorSet.GetLayout(), m_PrevWorldDescriptorSets[0].GetLayout() })
      };
      m_ForwardPipelines[7] = m_PSOCache.Register(ctx.device, mainRP, alphaTestInstInfo, pipelineCache);
    }

    // Wireframe pipelines (debug view). Mirror forward pipelines in GBuffer pass but with
    // polygonMode LINE; depth bias wins against filled depth-prepass surfaces.
    {
      PipelineCreateInfo wfInfo = {
        .fragmentShaderFile = "gbuffer_wireframe.frag",
        .vertexShaderFile = "mesh.vert",
        .pushConstantSize = gbufferPushConstantSize,
        .depthWrite = false,
        .colorAttachmentCount = 3,
        .compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
        .polygonMode = VK_POLYGON_MODE_LINE,
        .depthBiasEnable = true,
        .vertexInputFormat = "f3|f2f3f4",
        .sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_DefaultMaterial.GetLayout(),
          m_PrevWorldDescriptorSets[0].GetLayout() })
      };

      // [0] normal, [1] doubleSided
      m_WireframePipelines[0] = m_PSOCache.Register(ctx.device, mainRP, wfInfo, pipelineCache);
      wfInfo.doubleSided = true;
      m_WireframePipelines[1] = m_PSOCache.Register(ctx.device, mainRP, wfInfo, pipelineCache);

      // [2] instanced, [3] instanced+doubleSided
      wfInfo.sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_DefaultMaterial.GetLayout(),
        m_InstanceDescriptorSet.GetLayout(), m_PrevWorldDescriptorSets[0].GetLayout() });
      wfInfo.vertexShaderFile = "mesh_instanced.vert";
      wfInfo.doubleSided = false;
      m_WireframePipelines[2] = m_PSOCache.Register(ctx.device, mainRP, wfInfo, pipelineCache);
      wfInfo.doubleSided = true;
      m_WireframePipelines[3] = m_PSOCache.Register(ctx.device, mainRP, wfInfo, pipelineCache);

      // [4] noShading
      wfInfo.sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_DefaultMaterial.GetLayout(),
        m_PrevWorldDescriptorSets[0].GetLayout() });
      wfInfo.doubleSided = true;
      wfInfo.vertexShaderFile = "mesh.vert";
      m_WireframePipelines[4] = m_PSOCache.Register(ctx.device, mainRP, wfInfo, pipelineCache);

      // [5] terrain
      PipelineCreateInfo wfTerrainInfo = {
        .fragmentShaderFile = "gbuffer_wireframe.frag",
        .vertexShaderFile = "mesh.vert",
        .pushConstantSize = gbufferPushConstantSize,
        .depthWrite = false,
        .colorAttachmentCount = 3,
        .compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
        .polygonMode = VK_POLYGON_MODE_LINE,
        .depthBiasEnable = true,
        .vertexInputFormat = "f3|f2f3f4",
        .sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_TerrainMaterial.GetLayout(),
          m_PrevWorldDescriptorSets[0].GetLayout() })
      };
      m_WireframePipelines[5] = m_PSOCache.Register(ctx.device, mainRP, wfTerrainInfo, pipelineCache);

      // [6] alpha-test non-instanced
      PipelineCreateInfo wfAlphaInfo = {
        .fragmentShaderFile = "gbuffer_wireframe.frag",
        .vertexShaderFile = "mesh.vert",
        .pushConstantSize = gbufferPushConstantSize,
        .depthWrite = false,
        .doubleSided = true,
        .colorAttachmentCount = 3,
        .compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
        .polygonMode = VK_POLYGON_MODE_LINE,
        .depthBiasEnable = true,
        .vertexInputFormat = "f3|f2f3f4",
        .sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_DefaultMaterial.GetLayout(),
          m_PrevWorldDescriptorSets[0].GetLayout() })
      };
      m_WireframePipelines[6] = m_PSOCache.Register(ctx.device, mainRP, wfAlphaInfo, pipelineCache);

      // [7] alpha-test instanced
      wfAlphaInfo.vertexShaderFile = "mesh_instanced.vert";
      wfAlphaInfo.sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_DefaultMaterial.GetLayout(),
        m_InstanceDescriptorSet.GetLayout(), m_PrevWorldDescriptorSets[0].GetLayout() });
      m_WireframePipelines[7] = m_PSOCache.Register(ctx.device, mainRP, wfAlphaInfo, pipelineCache);

      // Transparent wireframe variants - rendered in GBuffer pass (not transparent pass)
      // to produce a single-pass wireframe image via gbuffer0.
      PipelineCreateInfo wfTrInfo = {
        .fragmentShaderFile = "gbuffer_wireframe.frag",
        .vertexShaderFile = "mesh.vert",
        .pushConstantSize = gbufferPushConstantSize,
        .depthWrite = false,
        .colorAttachmentCount = 3,
        .compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
        .polygonMode = VK_POLYGON_MODE_LINE,
        .depthBiasEnable = true,
        .vertexInputFormat = "f3|f2f3f4",
        .sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_DefaultMaterial.GetLayout(),
          m_PrevWorldDescriptorSets[0].GetLayout() })
      };
      m_WireframeTransparentPipelines[0] = m_PSOCache.Register(ctx.device, mainRP, wfTrInfo, pipelineCache);
      wfTrInfo.doubleSided = true;
      m_WireframeTransparentPipelines[1] = m_PSOCache.Register(ctx.device, mainRP, wfTrInfo, pipelineCache);

      wfTrInfo.doubleSided = false;
      wfTrInfo.vertexShaderFile = "mesh_instanced.vert";
      wfTrInfo.sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_DefaultMaterial.GetLayout(),
        m_InstanceDescriptorSet.GetLayout(), m_PrevWorldDescriptorSets[0].GetLayout() });
      m_WireframeTransparentPipelines[2] = m_PSOCache.Register(ctx.device, mainRP, wfTrInfo, pipelineCache);
      wfTrInfo.doubleSided = true;
      m_WireframeTransparentPipelines[3] = m_PSOCache.Register(ctx.device, mainRP, wfTrInfo, pipelineCache);
    }

    // Swapchain descriptor sets (set 1 - set 0 is FrameUniformBuffer)
    SetDescription desc = {
      .set = 1,
      .bindings = {
        {
          { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          // TAA diagnostics: motion vectors and the pre-resolve frame
          { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          // SSGI diagnostics: denoised screen GI and the reprojected radiance
          { 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
        }
      }
    };
    m_SwapChainDescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      m_SwapChainDescriptorSets[i].Init(ctx, desc);
    }

    // TAA descriptor sets (set 1 - set 0 is FrameUniformBuffer)
    SetDescription taaDesc = {
      .set = 1,
      .bindings = {
        {
          { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
        }
      }
    };
    m_TAADescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      m_TAADescriptorSets[i].Init(ctx, taaDesc);
    }

#ifdef YA_EDITOR
    VkRenderPass quadRP = m_Graph.GetPassRenderPass(m_SceneComposePassIndex);
#else
    VkRenderPass quadRP = m_Graph.GetPassRenderPass(m_SwapchainPassIndex);
#endif

    // Create layout helpers for tonemap pipeline (set 2: exposure, set 3: bloom)
    SetDescription expReadLayoutDesc = {
      .set = 2,
      .bindings = {
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT },
      }
    };
    VulkanDescriptorSet expReadLayoutHelper;
    expReadLayoutHelper.Init(ctx, expReadLayoutDesc);

    SetDescription bloomReadLayoutDesc = {
      .set = 3,
      .bindings = {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
      }
    };
    VulkanDescriptorSet bloomReadLayoutHelper;
    bloomReadLayoutHelper.Init(ctx, bloomReadLayoutDesc);

    PipelineCreateInfo quadInfo = {
      .fragmentShaderFile = "tonemap.frag",
      .vertexShaderFile = "fullscreen.vert",
      .depthTesting = false,
      .vertexInputFormat = "",
      .sets = std::vector({
        m_FrameUniformBuffer.GetLayout(),
        m_SwapChainDescriptorSets[0].GetLayout(),
        expReadLayoutHelper.GetLayout(),
        bloomReadLayoutHelper.GetLayout(),
      })
    };
    m_QuadPipeline = m_PSOCache.Register(ctx.device, quadRP, quadInfo, pipelineCache);
    expReadLayoutHelper.Destroy();
    bloomReadLayoutHelper.Destroy();

#ifdef YA_EDITOR
    {
      VkRenderPass gizmoRP = m_Graph.GetPassRenderPass(m_GizmoPassIndex);
      m_GizmoRenderer.Init(ctx, m_PSOCache, gizmoRP, m_FrameUniformBuffer.GetLayout());
    }

    // Scene depth resampled to output resolution for the gizmo passes.
    m_DepthCopyDescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      SetDescription depthCopyDesc = {
        .set = 0,
        .bindings = {
          { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT }
        }
      };
      m_DepthCopyDescriptorSets[i].Init(ctx, depthCopyDesc);
    }

    PipelineCreateInfo depthCopyInfo = {
      .fragmentShaderFile = "depth_copy.frag",
      .vertexShaderFile = "fullscreen.vert",
      .colorAttachmentCount = 0,
      .compareOp = VK_COMPARE_OP_ALWAYS,
      .vertexInputFormat = "",
      .sets = std::vector({ m_DepthCopyDescriptorSets[0].GetLayout() })
    };
    m_DepthCopyPipeline = m_PSOCache.Register(ctx.device,
      m_Graph.GetPassRenderPass(m_SceneDepthUpscalePassIndex), depthCopyInfo, pipelineCache);
#endif

    VkRenderPass taaRP = m_Graph.GetPassRenderPass(m_TAAPassIndex);
    PipelineCreateInfo taaInfo = {
      .fragmentShaderFile = "taa.frag",
      .vertexShaderFile = "fullscreen.vert",
      .depthTesting = false,
      .vertexInputFormat = "",
      .sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_TAADescriptorSets[0].GetLayout() })
    };
    m_TAAPipeline = m_PSOCache.Register(ctx.device, taaRP, taaInfo, pipelineCache);

    // GTAO depth prefilter (compute): frame UBO in set 0, GTAO constants plus the source
    // depth and one storage view per output mip in set 1.
    m_GTAOPrefilterDescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      SetDescription prefilterDesc = {
        .set = 1,
        .bindings = {
          {
            { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
            { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT },
            { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT },
            { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT },
            { 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT },
            { 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT },
            { 6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT },
          }
        }
      };
      m_GTAOPrefilterDescriptorSets[i].Init(ctx, prefilterDesc);
      m_GTAOPrefilterDescriptorSets[i].WriteUniformBuffer(0,
        m_GTAOConstantsUBOs[i].Get(), sizeof(GTAOConstants));
    }

    m_GTAOPrefilterPipeline = m_PSOCache.RegisterCompute(ctx.device, "gtao_depth_prefilter.comp",
      {
        m_FrameUniformBuffer.GetLayout(),
        m_GTAOPrefilterDescriptorSets[0].GetLayout(),
      },
      0,
      pipelineCache);

    // SSGI radiance prefilter (compute): reprojected TAA history in, five radiance
    // mips out. The push constant is the one-frame history invalidation flag.
    m_SSGIPrefilterDescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      SetDescription ssgiPrefilterDesc = {
        .set = 1,
        .bindings = {
          {
            { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT },
            { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT },
            { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT },
            { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT },
            { 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT },
            { 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT },
            { 6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT },
            { 7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT },
          }
        }
      };
      m_SSGIPrefilterDescriptorSets[i].Init(ctx, ssgiPrefilterDesc);
    }

    m_SSGIRadiancePrefilterPipeline = m_PSOCache.RegisterCompute(ctx.device, "ssgi_radiance_prefilter.comp",
      {
        m_FrameUniformBuffer.GetLayout(),
        m_SSGIPrefilterDescriptorSets[0].GetLayout(),
      },
      sizeof(int),
      pipelineCache);

    VkRenderPass gtaoRP = m_Graph.GetPassRenderPass(m_GTAOPassIndex);

    m_GTAOPassDescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      SetDescription gtaoDesc = {
        .set = 1,
        .bindings = {
          {
            { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            // Reprojected radiance - only the SSGI permutation declares it in GLSL,
            // but the shared set layout carries it for both.
            { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          }
        }
      };
      m_GTAOPassDescriptorSets[i].Init(ctx, gtaoDesc);
      m_GTAOPassDescriptorSets[i].WriteUniformBuffer(0,
        m_GTAOConstantsUBOs[i].Get(), sizeof(GTAOConstants));
      m_GTAOPassDescriptorSets[i].WriteCombinedImageSampler(3,
        m_GTAOHilbertLUT.GetView(), m_GTAOHilbertLUT.GetSampler(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    PipelineCreateInfo gtaoInfo = {
      .fragmentShaderFile = "gtao.frag",
      .vertexShaderFile = "fullscreen.vert",
      .depthTesting = false,
      .colorAttachmentCount = 4,
      .vertexInputFormat = "",
      .sets = std::vector({
        m_FrameUniformBuffer.GetLayout(),
        m_GTAOPassDescriptorSets[0].GetLayout(),
      })
    };
    m_GTAOPipeline = m_PSOCache.Register(ctx.device, gtaoRP, gtaoInfo, pipelineCache);

    // Same pass, same attachments, same sets - only the fragment permutation differs.
    // The execute callback picks one of the two by b_SSGIEnabled.
    gtaoInfo.fragmentShaderFile = "gtao_ssgi.frag";
    m_GTAOSSGIPipeline = m_PSOCache.Register(ctx.device, gtaoRP, gtaoInfo, pipelineCache);

    VkRenderPass gtaoDenoiseRP = m_Graph.GetPassRenderPass(m_GTAODenoisePassIndex);

    m_GTAODenoiseDescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      SetDescription denoiseDesc = {
        .set = 1,
        .bindings = {
          {
            { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            // SSGI working targets, filtered with the same weights as AO
            { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          }
        }
      };
      m_GTAODenoiseDescriptorSets[i].Init(ctx, denoiseDesc);
      m_GTAODenoiseDescriptorSets[i].WriteUniformBuffer(0,
        m_GTAOConstantsUBOs[i].Get(), sizeof(GTAOConstants));
    }

    PipelineCreateInfo gtaoDenoiseInfo = {
      .fragmentShaderFile = "gtao_denoise.frag",
      .vertexShaderFile = "fullscreen.vert",
      .depthTesting = false,
      .colorAttachmentCount = 3,
      .vertexInputFormat = "",
      .sets = std::vector({
        m_FrameUniformBuffer.GetLayout(),
        m_GTAODenoiseDescriptorSets[0].GetLayout(),
      })
    };
    m_GTAODenoisePipeline = m_PSOCache.Register(ctx.device, gtaoDenoiseRP, gtaoDenoiseInfo, pipelineCache);

    // SSR descriptor sets and pipeline (5 bindings: litColor, depth, gbuffer1, gbuffer0, hiZ)
    VkRenderPass ssrRP = m_Graph.GetPassRenderPass(m_SSRPassIndex);

    m_SSRPassDescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      SetDescription ssrDesc = {
        .set = 1,
        .bindings = {
          {
            { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          }
        }
      };
      m_SSRPassDescriptorSets[i].Init(ctx, ssrDesc);
    }
    PipelineCreateInfo ssrPipelineDesc = {
      .fragmentShaderFile = "ssr.frag",
      .vertexShaderFile = "fullscreen.vert",
      .depthTesting = false,
      .vertexInputFormat = "",
      .sets = std::vector({
        m_FrameUniformBuffer.GetLayout(),
        m_SSRPassDescriptorSets[0].GetLayout(),
      })
    };
    m_SSRPipeline = m_PSOCache.Register(ctx.device, ssrRP, ssrPipelineDesc, pipelineCache);

    VkRenderPass deferredRP = m_Graph.GetPassRenderPass(m_DeferredLightingPassIndex);

    m_DeferredLightingDescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      SetDescription dlDesc = {
        .set = 1,
        .bindings = {
          {
            { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            // Denoised SSGI + bent normal
            { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          }
        }
      };
      m_DeferredLightingDescriptorSets[i].Init(ctx, dlDesc);
    }

    // IBL descriptor set (irradiance array, prefilter array, BRDF LUT, skybox cubemap,
    // probe SSBO, three SH volume atlases, volume validity, volume UBO)
    SetDescription iblDesc = {
      .set = 3,
      .bindings = {
        {
          { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 9, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT },
        }
      }
    };
    m_IBLDescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    VkDescriptorSetLayout iblLayout = VK_NULL_HANDLE;
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      if (i == 0)
      {
        m_IBLDescriptorSets[i].Init(ctx, iblDesc);
        iblLayout = m_IBLDescriptorSets[i].GetLayout();
      }
      else
      {
        m_IBLDescriptorSets[i].Init(ctx, iblLayout);
      }
      m_IBLDescriptorSets[i].WriteCombinedImageSampler(0,
        m_ProbeAtlas.GetIrradianceView(), m_ProbeAtlas.GetIrradianceSampler());
      m_IBLDescriptorSets[i].WriteCombinedImageSampler(1,
        m_ProbeAtlas.GetPrefilterView(), m_ProbeAtlas.GetPrefilterSampler());
      m_IBLDescriptorSets[i].WriteCombinedImageSampler(2, m_NoneTexture.GetView(), m_NoneTexture.GetSampler());
      m_IBLDescriptorSets[i].WriteCombinedImageSampler(3, m_NoneCubeMap.GetView(), m_NoneCubeMap.GetSampler());
      m_IBLDescriptorSets[i].WriteStorageBuffer(4, m_ProbeBuffer.GetBuffer(uint32_t(i)), sizeof(ReflectionProbeBuffer));
    }
    WriteIrradianceVolumeDescriptors();

    // Deferred lighting set 2: lights SSBO (binding 0) + tile light indices SSBO (binding 1)
    m_DeferredLightingLightDescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      SetDescription dlLightDesc = {
        .set = 2,
        .bindings = {
          {
            { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          }
        }
      };
      m_DeferredLightingLightDescriptorSets[i].Init(ctx, dlLightDesc);
      m_DeferredLightingLightDescriptorSets[i].WriteStorageBuffer(0,
        m_LightBuffer.GetBuffer(uint32_t(i)), sizeof(LightBuffer));
      m_DeferredLightingLightDescriptorSets[i].WriteStorageBuffer(1,
        m_TileLightBuffer.GetBuffer(uint32_t(i)),
        m_TileLightBuffer.GetTileCountX() * m_TileLightBuffer.GetTileCountY() * sizeof(TileData));
      m_DeferredLightingLightDescriptorSets[i].WriteUniformBuffer(2,
        m_ShadowManager.GetShadowUBOBuffer(uint32_t(i)), sizeof(ShadowBuffer));
      m_DeferredLightingLightDescriptorSets[i].WriteCombinedImageSampler(3,
        m_ShadowManager.GetAtlas().GetView(), m_ShadowManager.GetAtlas().GetSampler(),
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    }

    PipelineCreateInfo deferredInfo = {
      .fragmentShaderFile = "deferred_lighting.frag",
      .vertexShaderFile = "fullscreen.vert",
      .depthTesting = false,
      .vertexInputFormat = "",
      .sets = std::vector({
        m_FrameUniformBuffer.GetLayout(),
        m_DeferredLightingDescriptorSets[0].GetLayout(),
        m_DeferredLightingLightDescriptorSets[0].GetLayout(),
        m_IBLDescriptorSets[0].GetLayout(),
      })
    };
    m_DeferredLightingPipeline = m_PSOCache.Register(ctx.device, deferredRP, deferredInfo, pipelineCache);

    // Forward Transparent pipelines - same lights/IBL/material descriptor sets as deferred,
    // depth LOAD/test/GEQUAL, src-alpha blending, output to SSRColor so the resolve sees it.
    {
      VkRenderPass transparentRP = m_Graph.GetPassRenderPass(m_ForwardTransparentPassIndex);

      // mesh_transparent is the TRANSPARENT permutation of mesh.vert: the shared
      // default permutation now carries a previous-world set the layouts here cannot host.
      PipelineCreateInfo trInfo = {
        .fragmentShaderFile = "forward_transparent.frag",
        .vertexShaderFile = "mesh_transparent.vert",
        .pushConstantSize = sizeof(glm::mat4) + sizeof(int),
        // Depth stays opaque-only: everything downstream samples m_MainDepth, and
        // transparent surfaces are already sorted back to front on the CPU.
        .depthWrite = false,
        .blending = true,
        .colorAttachmentCount = 1,
        .compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
        .vertexInputFormat = "f3|f2f3f4",
        .sets = std::vector({
          m_FrameUniformBuffer.GetLayout(),
          m_DefaultMaterial.GetLayout(),
          m_DeferredLightingLightDescriptorSets[0].GetLayout(),
          m_IBLDescriptorSets[0].GetLayout(),
        })
      };

      // [0] normal, [1] doubleSided
      m_ForwardTransparentPipelines[0] = m_PSOCache.Register(ctx.device, transparentRP, trInfo, pipelineCache);
      trInfo.doubleSided = true;
      m_ForwardTransparentPipelines[1] = m_PSOCache.Register(ctx.device, transparentRP, trInfo, pipelineCache);

      // [2] instanced, [3] instanced + doubleSided
      // Uses mesh_transparent_instanced, which binds Instances at set=4 since set=2 is taken by the lights buffer here.
      trInfo.doubleSided = false;
      trInfo.vertexShaderFile = "mesh_transparent_instanced.vert";
      trInfo.sets.push_back(m_InstanceDescriptorSet.GetLayout());
      m_ForwardTransparentPipelines[2] = m_PSOCache.Register(ctx.device, transparentRP, trInfo, pipelineCache);
      trInfo.doubleSided = true;
      m_ForwardTransparentPipelines[3] = m_PSOCache.Register(ctx.device, transparentRP, trInfo, pipelineCache);
    }

    m_LightCullInputDescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      SetDescription lcDesc = {
        .set = 1,
        .bindings = {
          {
            { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
            { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT },
          }
        }
      };
      m_LightCullInputDescriptorSets[i].Init(ctx, lcDesc);
      m_LightCullInputDescriptorSets[i].WriteStorageBuffer(0,
        m_LightBuffer.GetBuffer(uint32_t(i)), sizeof(LightBuffer));
    }

    m_LightCullPipeline = m_PSOCache.RegisterCompute(ctx.device, "light_cull.comp",
      {
        m_FrameUniformBuffer.GetLayout(),
        m_LightCullInputDescriptorSets[0].GetLayout(),
        m_TileLightBuffer.GetLayout(),
      },
      0,
      pipelineCache);

    SetDescription hizSetDesc = {
      .set = 0,
      .bindings = {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT }
      }
    };
    VulkanDescriptorSet hizLayoutHelper;
    hizLayoutHelper.Init(ctx, hizSetDesc);
    m_HiZPipeline = m_PSOCache.RegisterCompute(ctx.device, "hiz.comp",
      { hizLayoutHelper.GetLayout() },
      sizeof(int) * 5,
      pipelineCache);
    hizLayoutHelper.Destroy();

    m_HistogramBuffer.Create(ctx, HISTOGRAM_BIN_COUNT * sizeof(uint32_t));
    m_ExposureBuffer.Create(ctx, sizeof(float));
    float initialExposure = 0.1f;
    m_ExposureBuffer.Update(0, &initialExposure, sizeof(float));

    // Histogram pass descriptor sets (set 1: HDR sampler, set 2: histogram SSBO)
    SetDescription histInputDesc = {
      .set = 1,
      .bindings = {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT },
      }
    };
    SetDescription histOutputDesc = {
      .set = 2,
      .bindings = {
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
      }
    };
    m_HistogramPassDescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      m_HistogramPassDescriptorSets[i].Init(ctx, histInputDesc);
    }
    m_HistogramOutputDescriptorSet.Init(ctx, histOutputDesc);
    m_HistogramOutputDescriptorSet.WriteStorageBuffer(0, m_HistogramBuffer.Get(), HISTOGRAM_BIN_COUNT * sizeof(uint32_t));

    m_ExposureHistogramPipeline = m_PSOCache.RegisterCompute(ctx.device, "exposure_histogram.comp",
      {
        m_FrameUniformBuffer.GetLayout(),
        m_HistogramPassDescriptorSets[0].GetLayout(),
        m_HistogramOutputDescriptorSet.GetLayout(),
      },
      0,
      pipelineCache);

    // Exposure adapt descriptor sets (set 1: histogram SSBO + exposure SSBO)
    SetDescription adaptDesc = {
      .set = 1,
      .bindings = {
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
      }
    };
    m_ExposureAdaptDescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      m_ExposureAdaptDescriptorSets[i].Init(ctx, adaptDesc);
      m_ExposureAdaptDescriptorSets[i].WriteStorageBuffer(0, m_HistogramBuffer.Get(), HISTOGRAM_BIN_COUNT * sizeof(uint32_t));
      m_ExposureAdaptDescriptorSets[i].WriteStorageBuffer(1, m_ExposureBuffer.Get(), sizeof(float));
    }

    m_ExposureAdaptPipeline = m_PSOCache.RegisterCompute(ctx.device, "exposure_adapt.comp",
      {
        m_FrameUniformBuffer.GetLayout(),
        m_ExposureAdaptDescriptorSets[0].GetLayout(),
      },
      sizeof(ExposureAdaptPushConstants),
      pipelineCache);

    SetDescription bloomPipelineSetDesc = {
      .set = 0,
      .bindings = {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT }
      }
    };
    VulkanDescriptorSet bloomPipelineLayoutHelper;
    bloomPipelineLayoutHelper.Init(ctx, bloomPipelineSetDesc);
    m_BloomDownsamplePipeline = m_PSOCache.RegisterCompute(ctx.device, "bloom_downsample.comp",
      { bloomPipelineLayoutHelper.GetLayout() },
      sizeof(BloomPushConstants),
      pipelineCache);
    m_BloomUpsamplePipeline = m_PSOCache.RegisterCompute(ctx.device, "bloom_upsample.comp",
      { bloomPipelineLayoutHelper.GetLayout() },
      sizeof(BloomPushConstants),
      pipelineCache);
    bloomPipelineLayoutHelper.Destroy();

    // Exposure read descriptor sets for tonemap pass (set 2: exposure SSBO)
    SetDescription expReadDesc = {
      .set = 2,
      .bindings = {
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT },
      }
    };
    m_ExposureReadDescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      m_ExposureReadDescriptorSets[i].Init(ctx, expReadDesc);
      m_ExposureReadDescriptorSets[i].WriteStorageBuffer(0, m_ExposureBuffer.Get(), sizeof(float));
    }

    // Particle system - per-frame mapped SSBO + pooled descriptor sets (SSBO + sampler),
    // rendered via additive-blended triangle strip in the forward transparent pass.
    {
      const uint32_t framesInFlight = m_Backend.GetMaxFramesInFlight();
      const VkDeviceSize particleBufferSize = MAX_PARTICLES_PER_FRAME * sizeof(ParticleInstance);

      m_ParticleInstanceBuffers.resize(framesInFlight);
      for (uint32_t i = 0; i < framesInFlight; i++)
        m_ParticleInstanceBuffers[i].Create(ctx, particleBufferSize);

      SetDescription particleDesc = {
        .set = 1,
        .bindings = {
          { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
          { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
        }
      };

      m_ParticleDescriptorSets.resize(framesInFlight * MAX_PARTICLE_BATCHES_PER_FRAME);
      VkDescriptorSetLayout particleLayout = VK_NULL_HANDLE;
      for (size_t i = 0; i < m_ParticleDescriptorSets.size(); i++)
      {
        if (i == 0)
        {
          m_ParticleDescriptorSets[i].Init(ctx, particleDesc);
          particleLayout = m_ParticleDescriptorSets[i].GetLayout();
        }
        else
        {
          m_ParticleDescriptorSets[i].Init(ctx, particleLayout);
        }
      }

      // Pre-write the SSBO binding (one per frame-in-flight, replicated across batches).
      for (uint32_t f = 0; f < framesInFlight; f++)
      {
        for (uint32_t b = 0; b < MAX_PARTICLE_BATCHES_PER_FRAME; b++)
        {
          m_ParticleDescriptorSets[f * MAX_PARTICLE_BATCHES_PER_FRAME + b]
            .WriteStorageBuffer(0, m_ParticleInstanceBuffers[f].Get(), particleBufferSize);
        }
      }

      VkRenderPass transparentRP = m_Graph.GetPassRenderPass(m_ForwardTransparentPassIndex);
      PipelineCreateInfo particlePipelineInfo = {
        .fragmentShaderFile = "particle.frag",
        .vertexShaderFile = "particle.vert",
        .pushConstantSize = 4,
        .depthTesting = true,
        .depthWrite = false,
        .additiveBlend = true,
        .doubleSided = true,
        .colorAttachmentCount = 1,
        .compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
        .vertexInputFormat = "",
        .sets = std::vector({
          m_FrameUniformBuffer.GetLayout(),
          particleLayout,
        })
      };
      m_ParticlePipeline = m_PSOCache.Register(ctx.device, transparentRP, particlePipelineInfo, pipelineCache);

      m_ParticleStage.reserve(MAX_PARTICLES_PER_FRAME);
      m_PendingParticleBatches.reserve(MAX_PARTICLE_BATCHES_PER_FRAME);
    }

#ifdef YA_EDITOR
    // Pick id pipelines reuse existing depth (no write, GEQUAL) for free occlusion; alpha-test needs a real discard so a punched-out fragment doesn't wrongly claim the pixel, hence the dedicated variants [4] and [5].
    {
      VkRenderPass pickRP = m_Graph.GetPassRenderPass(m_PickIdPassIndex);
      constexpr uint32_t pickPushConstantSize = sizeof(glm::mat4) + sizeof(int) + sizeof(uint32_t);

      PipelineCreateInfo pickInfo = {
        .fragmentShaderFile = "pick_id.frag",
        .vertexShaderFile = "mesh_pick.vert",
        .pushConstantSize = pickPushConstantSize,
        .depthWrite = false,
        .compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
        .vertexInputFormat = "f3",
        .sets = std::vector({ m_FrameUniformBuffer.GetLayout() })
      };

      // [0] normal, [1] doubleSided
      m_PickPipelines[0] = m_PSOCache.Register(ctx.device, pickRP, pickInfo, pipelineCache);
      pickInfo.doubleSided = true;
      m_PickPipelines[1] = m_PSOCache.Register(ctx.device, pickRP, pickInfo, pipelineCache);

      // [2] instanced, [3] instanced+doubleSided
      pickInfo.doubleSided = false;
      pickInfo.vertexShaderFile = "mesh_instanced_pick.vert";
      pickInfo.sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_InstanceDescriptorSet.GetLayout() });
      m_PickPipelines[2] = m_PSOCache.Register(ctx.device, pickRP, pickInfo, pipelineCache);
      pickInfo.doubleSided = true;
      m_PickPipelines[3] = m_PSOCache.Register(ctx.device, pickRP, pickInfo, pipelineCache);

      // [4] alpha-test, [5] alpha-test instanced
      PipelineCreateInfo pickAlphaInfo = {
        .fragmentShaderFile = "pick_id_alphatest.frag",
        .vertexShaderFile = "mesh_pick_alphatest.vert",
        .pushConstantSize = pickPushConstantSize,
        .depthWrite = false,
        .doubleSided = true,
        .compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
        .vertexInputFormat = "f3|f2f3f4",
        .sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_DefaultMaterial.GetLayout() })
      };
      m_PickPipelines[4] = m_PSOCache.Register(ctx.device, pickRP, pickAlphaInfo, pipelineCache);

      pickAlphaInfo.vertexShaderFile = "mesh_instanced_pick_alphatest.vert";
      pickAlphaInfo.sets = std::vector({
        m_FrameUniformBuffer.GetLayout(),
        m_DefaultMaterial.GetLayout(),
        m_InstanceDescriptorSet.GetLayout()
      });
      m_PickPipelines[5] = m_PSOCache.Register(ctx.device, pickRP, pickAlphaInfo, pipelineCache);
    }
#endif
  }

#ifdef YA_EDITOR
  void Render::InitBackfaceMaskPipelines(VkRenderPass renderPass)
  {
    auto& ctx = m_Backend.GetContext();

    // doubleSided (VK_CULL_MODE_NONE) is intentional: with back faces culled, a node behind a wall would never see it, defeating the classification. Depth write plus the usual reversed-Z GREATER keeps the nearest surface in the mask regardless of facing.
    PipelineCreateInfo maskInfo = {
      .fragmentShaderFile = "backface_mask.frag",
      .vertexShaderFile = "mesh_depth.vert",
      .pushConstantSize = sizeof(glm::mat4) + sizeof(int),
      .doubleSided = true,
      .vertexInputFormat = "f3",
      .sets = std::vector({ m_FrameUniformBuffer.GetLayout() })
    };
    m_BackfaceMaskPipelines[0] = m_PSOCache.Register(ctx.device, renderPass, maskInfo,
      ctx.pipelineCache);

    maskInfo.vertexShaderFile = "mesh_instanced_depth.vert";
    maskInfo.sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_InstanceDescriptorSet.GetLayout() });
    m_BackfaceMaskPipelines[1] = m_PSOCache.Register(ctx.device, renderPass, maskInfo,
      ctx.pipelineCache);
  }
#endif
}
