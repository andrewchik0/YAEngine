#include "Render.h"
#include "BloomData.h"
#include "ExposureData.h"

namespace YAEngine
{
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

  VulkanPipeline& Render::GetDepthPipeline(const DrawCommand& dc)
  {
    assert(!dc.noShading && "Depth pipeline not available for noShading draw commands");
    return m_PSOCache.Get(m_DepthPipelines[dc.SortKey()]);
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
      PipelineCreateInfo shadowInfo = {
        .vertexShaderFile = "shadow.vert",
        .pushConstantSize = sizeof(glm::mat4) + sizeof(int) + sizeof(int),
        .colorAttachmentCount = 0,
        .vertexInputFormat = "f3",
        .sets = std::vector({ m_ShadowManager.GetShadowCascadeUBOLayout() })
      };
      shadowInfo.depthBiasEnable = true;

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
        .pushConstantSize = sizeof(glm::mat4) + sizeof(int) + sizeof(int),
        .doubleSided = true,
        .colorAttachmentCount = 0,
        .vertexInputFormat = "f3|f2f3f4",
        .sets = std::vector({ m_ShadowManager.GetShadowCascadeUBOLayout(), m_DefaultMaterial.GetLayout() })
      };
      shadowAlphaInfo.depthBiasEnable = true;
      m_ShadowPipelines[6] = m_PSOCache.Register(ctx.device, shadowRP, shadowAlphaInfo, pipelineCache);

      shadowAlphaInfo.vertexShaderFile = "shadow_instanced_alphatest.vert";
      shadowAlphaInfo.sets = std::vector({ m_ShadowManager.GetShadowCascadeUBOLayout(), m_DefaultMaterial.GetLayout(), m_InstanceDescriptorSet.GetLayout() });
      m_ShadowPipelines[7] = m_PSOCache.Register(ctx.device, shadowRP, shadowAlphaInfo, pipelineCache);
    }

    VkRenderPass mainRP = m_Graph.GetPassRenderPass(m_GBufferPassIndex);

    PipelineCreateInfo forwardInfo = {
      .fragmentShaderFile = "gbuffer.frag",
      .vertexShaderFile = "mesh.vert",
      .pushConstantSize = sizeof(glm::mat4) + sizeof(int),
      .depthWrite = false,
      .colorAttachmentCount = 3,
      .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
      .vertexInputFormat = "f3|f2f3f4",
      .sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_DefaultMaterial.GetLayout() })
    };

    // [0] normal, [1] doubleSided
    m_ForwardPipelines[0] = m_PSOCache.Register(ctx.device, mainRP, forwardInfo, pipelineCache);
    forwardInfo.doubleSided = true;
    m_ForwardPipelines[1] = m_PSOCache.Register(ctx.device, mainRP, forwardInfo, pipelineCache);

    // [2] instanced, [3] instanced+doubleSided
    forwardInfo.sets.push_back(m_InstanceDescriptorSet.GetLayout());
    forwardInfo.vertexShaderFile = "mesh_instanced.vert";
    forwardInfo.doubleSided = false;
    m_ForwardPipelines[2] = m_PSOCache.Register(ctx.device, mainRP, forwardInfo, pipelineCache);
    forwardInfo.doubleSided = true;
    m_ForwardPipelines[3] = m_PSOCache.Register(ctx.device, mainRP, forwardInfo, pipelineCache);

    // [4] noShading
    forwardInfo.sets.pop_back();
    forwardInfo.doubleSided = true;
    forwardInfo.fragmentShaderFile = "gbuffer_unlit.frag";
    forwardInfo.vertexShaderFile = "mesh.vert";
    m_ForwardPipelines[4] = m_PSOCache.Register(ctx.device, mainRP, forwardInfo, pipelineCache);

    // [5] terrain (two-layer splatting)
    {
      PipelineCreateInfo terrainInfo = {
        .fragmentShaderFile = "gbuffer_terrain.frag",
        .vertexShaderFile = "mesh.vert",
        .pushConstantSize = sizeof(glm::mat4) + sizeof(int),
        .depthWrite = false,
        .colorAttachmentCount = 3,
        .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .vertexInputFormat = "f3|f2f3f4",
        .sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_TerrainMaterial.GetLayout() })
      };
      m_ForwardPipelines[5] = m_PSOCache.Register(ctx.device, mainRP, terrainInfo, pipelineCache);
    }

    // [6] alpha-test non-instanced (depth from prepass - no depth write, LEQUAL)
    {
      PipelineCreateInfo alphaTestInfo = {
        .fragmentShaderFile = "gbuffer_alphatest.frag",
        .vertexShaderFile = "mesh.vert",
        .pushConstantSize = sizeof(glm::mat4) + sizeof(int),
        .depthWrite = false,
        .doubleSided = true,
        .colorAttachmentCount = 3,
        .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .vertexInputFormat = "f3|f2f3f4",
        .sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_DefaultMaterial.GetLayout() })
      };
      m_ForwardPipelines[6] = m_PSOCache.Register(ctx.device, mainRP, alphaTestInfo, pipelineCache);
    }

    // [7] alpha-test instanced (depth from prepass - no depth write, LEQUAL)
    {
      PipelineCreateInfo alphaTestInstInfo = {
        .fragmentShaderFile = "gbuffer_alphatest.frag",
        .vertexShaderFile = "mesh_instanced.vert",
        .pushConstantSize = sizeof(glm::mat4) + sizeof(int),
        .depthWrite = false,
        .doubleSided = true,
        .colorAttachmentCount = 3,
        .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .vertexInputFormat = "f3|f2f3f4",
        .sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_DefaultMaterial.GetLayout(), m_InstanceDescriptorSet.GetLayout() })
      };
      m_ForwardPipelines[7] = m_PSOCache.Register(ctx.device, mainRP, alphaTestInstInfo, pipelineCache);
    }

    // Wireframe pipelines (debug view). Mirror forward pipelines in GBuffer pass but with
    // polygonMode LINE; depth bias wins against filled depth-prepass surfaces.
    {
      PipelineCreateInfo wfInfo = {
        .fragmentShaderFile = "gbuffer_wireframe.frag",
        .vertexShaderFile = "mesh.vert",
        .pushConstantSize = sizeof(glm::mat4) + sizeof(int),
        .depthWrite = false,
        .colorAttachmentCount = 3,
        .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .polygonMode = VK_POLYGON_MODE_LINE,
        .depthBiasEnable = true,
        .vertexInputFormat = "f3|f2f3f4",
        .sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_DefaultMaterial.GetLayout() })
      };

      // [0] normal, [1] doubleSided
      m_WireframePipelines[0] = m_PSOCache.Register(ctx.device, mainRP, wfInfo, pipelineCache);
      wfInfo.doubleSided = true;
      m_WireframePipelines[1] = m_PSOCache.Register(ctx.device, mainRP, wfInfo, pipelineCache);

      // [2] instanced, [3] instanced+doubleSided
      wfInfo.sets.push_back(m_InstanceDescriptorSet.GetLayout());
      wfInfo.vertexShaderFile = "mesh_instanced.vert";
      wfInfo.doubleSided = false;
      m_WireframePipelines[2] = m_PSOCache.Register(ctx.device, mainRP, wfInfo, pipelineCache);
      wfInfo.doubleSided = true;
      m_WireframePipelines[3] = m_PSOCache.Register(ctx.device, mainRP, wfInfo, pipelineCache);

      // [4] noShading
      wfInfo.sets.pop_back();
      wfInfo.doubleSided = true;
      wfInfo.vertexShaderFile = "mesh.vert";
      m_WireframePipelines[4] = m_PSOCache.Register(ctx.device, mainRP, wfInfo, pipelineCache);

      // [5] terrain
      PipelineCreateInfo wfTerrainInfo = {
        .fragmentShaderFile = "gbuffer_wireframe.frag",
        .vertexShaderFile = "mesh.vert",
        .pushConstantSize = sizeof(glm::mat4) + sizeof(int),
        .depthWrite = false,
        .colorAttachmentCount = 3,
        .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .polygonMode = VK_POLYGON_MODE_LINE,
        .depthBiasEnable = true,
        .vertexInputFormat = "f3|f2f3f4",
        .sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_TerrainMaterial.GetLayout() })
      };
      m_WireframePipelines[5] = m_PSOCache.Register(ctx.device, mainRP, wfTerrainInfo, pipelineCache);

      // [6] alpha-test non-instanced
      PipelineCreateInfo wfAlphaInfo = {
        .fragmentShaderFile = "gbuffer_wireframe.frag",
        .vertexShaderFile = "mesh.vert",
        .pushConstantSize = sizeof(glm::mat4) + sizeof(int),
        .depthWrite = false,
        .doubleSided = true,
        .colorAttachmentCount = 3,
        .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .polygonMode = VK_POLYGON_MODE_LINE,
        .depthBiasEnable = true,
        .vertexInputFormat = "f3|f2f3f4",
        .sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_DefaultMaterial.GetLayout() })
      };
      m_WireframePipelines[6] = m_PSOCache.Register(ctx.device, mainRP, wfAlphaInfo, pipelineCache);

      // [7] alpha-test instanced
      wfAlphaInfo.vertexShaderFile = "mesh_instanced.vert";
      wfAlphaInfo.sets.push_back(m_InstanceDescriptorSet.GetLayout());
      m_WireframePipelines[7] = m_PSOCache.Register(ctx.device, mainRP, wfAlphaInfo, pipelineCache);

      // Transparent wireframe variants - rendered in GBuffer pass (not transparent pass)
      // to produce a single-pass wireframe image via gbuffer0.
      PipelineCreateInfo wfTrInfo = {
        .fragmentShaderFile = "gbuffer_wireframe.frag",
        .vertexShaderFile = "mesh.vert",
        .pushConstantSize = sizeof(glm::mat4) + sizeof(int),
        .depthWrite = false,
        .colorAttachmentCount = 3,
        .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .polygonMode = VK_POLYGON_MODE_LINE,
        .depthBiasEnable = true,
        .vertexInputFormat = "f3|f2f3f4",
        .sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_DefaultMaterial.GetLayout() })
      };
      m_WireframeTransparentPipelines[0] = m_PSOCache.Register(ctx.device, mainRP, wfTrInfo, pipelineCache);
      wfTrInfo.doubleSided = true;
      m_WireframeTransparentPipelines[1] = m_PSOCache.Register(ctx.device, mainRP, wfTrInfo, pipelineCache);

      wfTrInfo.doubleSided = false;
      wfTrInfo.vertexShaderFile = "mesh_instanced.vert";
      wfTrInfo.sets.push_back(m_InstanceDescriptorSet.GetLayout());
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
        }
      }
    };
    m_TAADescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      m_TAADescriptorSets[i].Init(ctx, taaDesc);
    }

    // Quad pipeline - tone mapping pass
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
#endif

    // TAA pipeline - use TAA render pass (compatible format)
    VkRenderPass taaRP = m_Graph.GetPassRenderPass(m_TAAPassIndex);
    PipelineCreateInfo taaInfo = {
      .fragmentShaderFile = "taa.frag",
      .vertexShaderFile = "fullscreen.vert",
      .depthTesting = false,
      .vertexInputFormat = "",
      .sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_TAADescriptorSets[0].GetLayout() })
    };
    m_TAAPipeline = m_PSOCache.Register(ctx.device, taaRP, taaInfo, pipelineCache);

    // SSAO descriptor sets and pipeline
    VkRenderPass ssaoRP = m_Graph.GetPassRenderPass(m_SSAOPassIndex);

    m_SSAOPassDescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      SetDescription ssaoDesc = {
        .set = 1,
        .bindings = {
          {
            { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT },
          }
        }
      };
      m_SSAOPassDescriptorSets[i].Init(ctx, ssaoDesc);
      // Write static bindings (noise texture + kernel UBO)
      m_SSAOPassDescriptorSets[i].WriteCombinedImageSampler(2,
        m_SSAONoise.GetView(), m_SSAONoise.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      m_SSAOPassDescriptorSets[i].WriteUniformBuffer(3, m_SSAOKernelUBO.Get(), 16 * sizeof(glm::vec4));
    }
    PipelineCreateInfo ssaoInfo = {
      .fragmentShaderFile = "ssao.frag",
      .vertexShaderFile = "fullscreen.vert",
      .depthTesting = false,
      .vertexInputFormat = "",
      .sets = std::vector({
        m_FrameUniformBuffer.GetLayout(),
        m_SSAOPassDescriptorSets[0].GetLayout(),
      })
    };
    m_SSAOPipeline = m_PSOCache.Register(ctx.device, ssaoRP, ssaoInfo, pipelineCache);

    // SSAO Blur descriptor sets and pipelines (horizontal + vertical)
    SetDescription ssaoBlurDesc = {
      .set = 1,
      .bindings = {
        {
          { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
        }
      }
    };

    m_SSAOBlurHPassDescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      m_SSAOBlurHPassDescriptorSets[i].Init(ctx, ssaoBlurDesc);
    }

    m_SSAOBlurVPassDescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      m_SSAOBlurVPassDescriptorSets[i].Init(ctx, ssaoBlurDesc);
    }

    PipelineCreateInfo ssaoBlurInfo = {
      .fragmentShaderFile = "ssao_blur.frag",
      .vertexShaderFile = "fullscreen.vert",
      .pushConstantSize = sizeof(int),
      .depthTesting = false,
      .vertexInputFormat = "",
      .sets = std::vector({
        m_FrameUniformBuffer.GetLayout(),
        m_SSAOBlurHPassDescriptorSets[0].GetLayout(),
      })
    };

    VkRenderPass ssaoBlurHRP = m_Graph.GetPassRenderPass(m_SSAOBlurHPassIndex);
    m_SSAOBlurHPipeline = m_PSOCache.Register(ctx.device, ssaoBlurHRP, ssaoBlurInfo, pipelineCache);

    VkRenderPass ssaoBlurVRP = m_Graph.GetPassRenderPass(m_SSAOBlurVPassIndex);
    m_SSAOBlurVPipeline = m_PSOCache.Register(ctx.device, ssaoBlurVRP, ssaoBlurInfo, pipelineCache);

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

    // Deferred Lighting descriptor sets and pipeline
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
          }
        }
      };
      m_DeferredLightingDescriptorSets[i].Init(ctx, dlDesc);
    }

    // IBL descriptor set (irradiance array, prefilter array, BRDF LUT, skybox cubemap, probe SSBO)
    SetDescription iblDesc = {
      .set = 3,
      .bindings = {
        {
          { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT },
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
      m_IBLDescriptorSets[i].WriteStorageBuffer(4, m_ProbeBuffer.GetBuffer(uint32_t(i)), sizeof(LightProbeBuffer));
    }

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
    // depth LOAD/write/LEQUAL, src-alpha blending, output to TAAHistory0.
    {
      VkRenderPass transparentRP = m_Graph.GetPassRenderPass(m_ForwardTransparentPassIndex);

      PipelineCreateInfo trInfo = {
        .fragmentShaderFile = "forward_transparent.frag",
        .vertexShaderFile = "mesh.vert",
        .pushConstantSize = sizeof(glm::mat4) + sizeof(int),
        .depthWrite = true,
        .blending = true,
        .colorAttachmentCount = 1,
        .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
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
      // Use mesh_transparent_instanced permutation that binds Instances at set=4
      // (set=2 is taken by lights buffer in transparent pipeline layout).
      trInfo.doubleSided = false;
      trInfo.vertexShaderFile = "mesh_transparent_instanced.vert";
      trInfo.sets.push_back(m_InstanceDescriptorSet.GetLayout());
      m_ForwardTransparentPipelines[2] = m_PSOCache.Register(ctx.device, transparentRP, trInfo, pipelineCache);
      trInfo.doubleSided = true;
      m_ForwardTransparentPipelines[3] = m_PSOCache.Register(ctx.device, transparentRP, trInfo, pipelineCache);
    }

    // Light cull compute pipeline - descriptor sets
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

    // Hi-Z compute pipeline
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
      sizeof(int) * 3,
      pipelineCache);
    hizLayoutHelper.Destroy();

    // Auto Exposure - buffers
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

    // Bloom compute pipelines
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

  }
}
