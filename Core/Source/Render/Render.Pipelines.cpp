#include "Render.h"

namespace YAEngine
{
  VulkanPipeline& Render::GetForwardPipeline(const DrawCommand& dc)
  {
    return m_PSOCache.Get(m_ForwardPipelines[dc.SortKey()]);
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
    constexpr auto MAX_INSTANCES = 10000;
    m_InstanceBuffer.Create(ctx, MAX_INSTANCES * sizeof(glm::mat4));
    m_InstanceDescriptorSet.WriteStorageBuffer(0, m_InstanceBuffer.Get(), MAX_INSTANCES * sizeof(glm::mat4));

    VkRenderPass depthRP = m_Graph.GetPassRenderPass(m_DepthPrepassIndex);

    PipelineCreateInfo depthInfo = {
      .vertexShaderFile = "mesh_depth.vert",
      .pushConstantSize = sizeof(glm::mat4) + sizeof(int),
      .colorAttachmentCount = 0,
      .vertexInputFormat = "f3f2f3f4",
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

    VkRenderPass mainRP = m_Graph.GetPassRenderPass(m_MainPassIndex);

    PipelineCreateInfo forwardInfo = {
      .fragmentShaderFile = "shader.frag",
      .vertexShaderFile = "mesh.vert",
      .pushConstantSize = sizeof(glm::mat4) + sizeof(int),
      .depthWrite = false,
      .colorAttachmentCount = 5,
      .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
      .vertexInputFormat = "f3f2f3f4",
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
    forwardInfo.fragmentShaderFile = "no_shading.frag";
    forwardInfo.vertexShaderFile = "mesh.vert";
    m_ForwardPipelines[4] = m_PSOCache.Register(ctx.device, mainRP, forwardInfo, pipelineCache);

    // Swapchain descriptor sets (set 1 — set 0 is FrameUniformBuffer)
    SetDescription desc = {
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
    m_SwapChainDescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      m_SwapChainDescriptorSets[i].Init(ctx, desc);
    }

    // TAA descriptor sets (set 1 — set 0 is FrameUniformBuffer)
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

    // Quad pipeline — tone mapping pass
#ifdef YA_EDITOR
    VkRenderPass quadRP = m_Graph.GetPassRenderPass(m_SceneComposePassIndex);
#else
    VkRenderPass quadRP = m_Graph.GetPassRenderPass(m_SwapchainPassIndex);
#endif

    PipelineCreateInfo quadInfo = {
      .fragmentShaderFile = "quad.frag",
      .vertexShaderFile = "quad.vert",
      .depthTesting = false,
      .vertexInputFormat = "",
      .sets = std::vector({
        m_FrameUniformBuffer.GetLayout(),
        m_SwapChainDescriptorSets[0].GetLayout(),
      })
    };
    m_QuadPipeline = m_PSOCache.Register(ctx.device, quadRP, quadInfo, pipelineCache);

    // TAA pipeline — use TAA render pass (compatible format)
    VkRenderPass taaRP = m_Graph.GetPassRenderPass(m_TAAPassIndex);
    PipelineCreateInfo taaInfo = {
      .fragmentShaderFile = "taa.frag",
      .vertexShaderFile = "quad.vert",
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
      .vertexShaderFile = "quad.vert",
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
      .vertexShaderFile = "quad.vert",
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

    // SSR descriptor sets and pipeline (7 bindings: frame, depth, normals, material, albedo, ssao, hiZ)
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
            { 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          }
        }
      };
      m_SSRPassDescriptorSets[i].Init(ctx, ssrDesc);
    }
    PipelineCreateInfo ssrPipelineDesc = {
      .fragmentShaderFile = "ssr.frag",
      .vertexShaderFile = "quad.vert",
      .depthTesting = false,
      .vertexInputFormat = "",
      .sets = std::vector({
        m_FrameUniformBuffer.GetLayout(),
        m_SSRPassDescriptorSets[0].GetLayout(),
      })
    };
    m_SSRPipeline = m_PSOCache.Register(ctx.device, ssrRP, ssrPipelineDesc, pipelineCache);

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
  }
}
