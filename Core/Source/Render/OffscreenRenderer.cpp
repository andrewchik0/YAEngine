#include "OffscreenRenderer.h"

#include "Render.h"
#include "VulkanCommandBuffer.h"
#include "ImageBarrier.h"
#include "DebugMarker.h"
#include "Utils/Log.h"

namespace YAEngine
{
  void OffscreenRenderer::Init(Render& render, uint32_t resolution)
  {
    m_Render = &render;
    m_Ctx = &render.GetContext();
    m_Resolution = resolution;

    m_FrameUBO.Init(*m_Ctx);
    SetupGraph(resolution);
    InitDescriptors();
  }

  void OffscreenRenderer::Destroy()
  {
    if (!m_Ctx) return;

    m_DeferredGBufferDescriptorSet.Destroy();
    m_LightCullInputDescriptorSet.Destroy();
    m_DeferredLightDescriptorSet.Destroy();
    m_TileLightBuffer.Destroy(*m_Ctx);
    m_FrameUBO.Destroy(*m_Ctx);
    m_Graph.Destroy();

    m_Ctx = nullptr;
    m_Render = nullptr;
  }

  void OffscreenRenderer::SetupGraph(uint32_t resolution)
  {
    m_Graph.Init(*m_Ctx, { resolution, resolution });

    // Same formats as main graph - guarantees VkRenderPass compatibility with main pipelines
    m_GBuffer0 = m_Graph.CreateResource({
      .name = "offGBuffer0",
      .format = VK_FORMAT_R8G8B8A8_UNORM
    });
    m_GBuffer1 = m_Graph.CreateResource({
      .name = "offGBuffer1",
      .format = VK_FORMAT_A2B10G10R10_UNORM_PACK32
    });
    m_MainDepth = m_Graph.CreateResource({
      .name = "offMainDepth",
      .format = VK_FORMAT_D32_SFLOAT,
      .aspect = VK_IMAGE_ASPECT_DEPTH_BIT
    });
    m_MainVelocity = m_Graph.CreateResource({
      .name = "offMainVelocity",
      .format = VK_FORMAT_R16G16_SFLOAT
    });
    m_LitColor = m_Graph.CreateResource({
      .name = "offLitColor",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT,
      .additionalUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT
    });

    // 1. Depth prepass
    m_DepthPrepassIndex = m_Graph.AddPass({
      .name = "OffDepthPrepass",
      .depthOutput = m_MainDepth,
      .clearDepth = true,
      .depthOnly = true,
      .execute = [this](const RGExecuteContext& ctx) {
        auto* frame = static_cast<FrameContext*>(ctx.userData);
        m_Render->DrawMeshesDepthOnly(ctx.cmd, 0, *frame, m_FrameUBO.GetDescriptorSet(0));
      }
    });

    // 2. GBuffer pass
    m_GBufferPassIndex = m_Graph.AddPass({
      .name = "OffGBufferPass",
      .colorOutputs = { m_GBuffer0, m_GBuffer1, m_MainVelocity },
      .depthOutput = m_MainDepth,
      .clearColor = true,
      .clearDepth = false,
      .execute = [this](const RGExecuteContext& ctx) {
        auto* frame = static_cast<FrameContext*>(ctx.userData);
        m_Render->DrawMeshes(ctx.cmd, 0, *frame, m_FrameUBO.GetDescriptorSet(0));
      }
    });

    // 3. Light cull (compute)
    m_LightCullPassIndex = m_Graph.AddPass({
      .name = "OffLightCull",
      .inputs = { m_MainDepth },
      .isCompute = true,
      .execute = [this](const RGExecuteContext& ctx) {
        auto& mainDepth = m_Graph.GetResource(m_MainDepth);

        m_LightCullInputDescriptorSet.WriteCombinedImageSampler(1,
          mainDepth.GetView(), mainDepth.GetSampler(),
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        auto& pipeline = m_Render->m_PSOCache.GetCompute(
          m_Render->m_LightCullPipeline);
        pipeline.Bind(ctx.cmd);
        pipeline.BindDescriptorSets(ctx.cmd, { m_FrameUBO.GetDescriptorSet(0) }, 0);
        pipeline.BindDescriptorSets(ctx.cmd, { m_LightCullInputDescriptorSet.Get() }, 1);
        pipeline.BindDescriptorSets(ctx.cmd, { m_TileLightBuffer.GetDescriptorSet(0) }, 2);

        pipeline.Dispatch(ctx.cmd,
          m_TileLightBuffer.GetTileCountX(),
          m_TileLightBuffer.GetTileCountY(), 1);

        // Buffer barrier: compute write -> fragment read
        VkBufferMemoryBarrier bufferBarrier {};
        bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        bufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.buffer = m_TileLightBuffer.GetBuffer(0);
        bufferBarrier.offset = 0;
        bufferBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(ctx.cmd,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          0, 0, nullptr, 1, &bufferBarrier, 0, nullptr);
      }
    });

    // 4. Deferred lighting
    m_DeferredLightingPassIndex = m_Graph.AddPass({
      .name = "OffDeferredLighting",
      .inputs = { m_GBuffer0, m_GBuffer1, m_MainDepth },
      .colorOutputs = { m_LitColor },
      .execute = [this](const RGExecuteContext& ctx) {
        auto& gbuffer0 = m_Graph.GetResource(m_GBuffer0);
        auto& gbuffer1 = m_Graph.GetResource(m_GBuffer1);
        auto& mainDepth = m_Graph.GetResource(m_MainDepth);

        auto& pipeline = m_Render->m_PSOCache.Get(
          m_Render->m_DeferredLightingPipeline);
        pipeline.Bind(ctx.cmd);

        // set 1: GBuffer textures + NoneTexture as SSAO dummy
        m_DeferredGBufferDescriptorSet.WriteCombinedImageSampler(0,
          gbuffer0.GetView(), gbuffer0.GetSampler(),
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_DeferredGBufferDescriptorSet.WriteCombinedImageSampler(1,
          gbuffer1.GetView(), gbuffer1.GetSampler(),
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_DeferredGBufferDescriptorSet.WriteCombinedImageSampler(2,
          mainDepth.GetView(), mainDepth.GetSampler(),
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        pipeline.BindDescriptorSets(ctx.cmd, { m_FrameUBO.GetDescriptorSet(0) }, 0);
        pipeline.BindDescriptorSets(ctx.cmd, { m_DeferredGBufferDescriptorSet.Get() }, 1);
        pipeline.BindDescriptorSets(ctx.cmd, { m_DeferredLightDescriptorSet.Get() }, 2);
        pipeline.BindDescriptorSets(ctx.cmd, { m_Render->m_IBLDescriptorSets[0].Get() }, 3);

        m_Render->DrawQuad(ctx.cmd);
      }
    });

    m_Graph.Compile();

    // Init tile light buffer for this resolution
    uint32_t tileCountX = (resolution + TILE_SIZE - 1) / TILE_SIZE;
    uint32_t tileCountY = (resolution + TILE_SIZE - 1) / TILE_SIZE;
    m_TileLightBuffer.Init(*m_Ctx, tileCountX, tileCountY);
  }

  void OffscreenRenderer::InitDescriptors()
  {
    // Deferred lighting set 1: GBuffer textures (3 samplers, matching deferred_lighting.frag)
    SetDescription dlGBufferDesc = {
      .set = 1,
      .bindings = {
        {
          { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
        }
      }
    };
    m_DeferredGBufferDescriptorSet.Init(*m_Ctx, dlGBufferDesc);

    // Light cull set 1: lights SSBO + depth sampler
    SetDescription lcDesc = {
      .set = 1,
      .bindings = {
        {
          { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
          { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT },
        }
      }
    };
    m_LightCullInputDescriptorSet.Init(*m_Ctx, lcDesc);
    // Bind lights SSBO from Render's LightStorageBuffer
    m_LightCullInputDescriptorSet.WriteStorageBuffer(0,
      m_Render->m_LightBuffer.GetBuffer(0), sizeof(LightBuffer));

    // Deferred lighting set 2: lights SSBO + tile SSBO + shadow UBO + shadow atlas
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
    m_DeferredLightDescriptorSet.Init(*m_Ctx, dlLightDesc);

    // Bind lights SSBO (binding 0)
    m_DeferredLightDescriptorSet.WriteStorageBuffer(0,
      m_Render->m_LightBuffer.GetBuffer(0), sizeof(LightBuffer));

    // Bind our tile SSBO (binding 1)
    VkDeviceSize tileBufferSize = m_TileLightBuffer.GetTileCountX() *
      m_TileLightBuffer.GetTileCountY() * sizeof(TileData);
    m_DeferredLightDescriptorSet.WriteStorageBuffer(1,
      m_TileLightBuffer.GetBuffer(0), tileBufferSize);

    // Bind shadow UBO (binding 2) + shadow atlas sampler (binding 3)
    auto& shadowMgr = m_Render->m_ShadowManager;
    m_DeferredLightDescriptorSet.WriteUniformBuffer(2,
      shadowMgr.GetShadowUBOBuffer(0), sizeof(ShadowBuffer));
    m_DeferredLightDescriptorSet.WriteCombinedImageSampler(3,
      shadowMgr.GetAtlas().GetView(), shadowMgr.GetAtlas().GetSampler(),
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
  }

  VulkanImage& OffscreenRenderer::RenderFace(FrameContext& frame,
    const glm::vec3& position, const glm::mat4& faceView, uint32_t resolution)
  {
    // Set up camera for this cubemap face
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.01f, 1000.0f);
    proj[1][1] *= -1.0f;

    glm::mat4 view = glm::translate(faceView, -position);

    auto& uniforms = m_FrameUBO.uniforms;
    uniforms.view = view;
    uniforms.proj = proj;
    uniforms.invProj = glm::inverse(proj);
    uniforms.invView = glm::inverse(view);
    uniforms.prevView = view;
    uniforms.prevProj = proj;
    uniforms.nearPlane = 0.01f;
    uniforms.farPlane = 1000.0f;
    uniforms.cameraPosition = position;
    uniforms.cameraDirection = glm::normalize(-glm::vec3(glm::inverse(faceView)[2]));
    uniforms.fov = glm::radians(90.0f);
    uniforms.screenWidth = int(resolution);
    uniforms.screenHeight = int(resolution);
    uniforms.tileCountX = (int(resolution) + TILE_SIZE - 1) / TILE_SIZE;
    uniforms.tileCountY = (int(resolution) + TILE_SIZE - 1) / TILE_SIZE;
    uniforms.ssaoEnabled = 0;
    uniforms.ssrEnabled = 0;
    uniforms.taaEnabled = 0;
    uniforms.hizMipCount = 0;
    uniforms.frameIndex = 0;
    uniforms.jitterX = 0.0f;
    uniforms.jitterY = 0.0f;
    uniforms.time = 0.0f;
    uniforms.gamma = 2.2f;
    uniforms.exposure = 1.0f;
    uniforms.currentTexture = 0;
    uniforms.tonemapMode = 0;
    uniforms.bloomIntensity = 0.0f;

    m_FrameUBO.SetUp(0);

    // Reset layout tracking so barriers are correct after external transitions
    // (LightProbeBaker copies LitColor between faces, changing layouts outside the graph)
    m_Graph.SetResourceLayout(m_GBuffer0, VK_IMAGE_LAYOUT_UNDEFINED);
    m_Graph.SetResourceLayout(m_GBuffer1, VK_IMAGE_LAYOUT_UNDEFINED);
    m_Graph.SetResourceLayout(m_MainDepth, VK_IMAGE_LAYOUT_UNDEFINED);
    m_Graph.SetResourceLayout(m_MainVelocity, VK_IMAGE_LAYOUT_UNDEFINED);
    m_Graph.SetResourceLayout(m_LitColor, VK_IMAGE_LAYOUT_UNDEFINED);

    VkCommandBuffer cmd = m_Ctx->commandBuffer->BeginSingleTimeCommands();
    m_Graph.Execute(cmd, &frame);
    m_Ctx->commandBuffer->EndSingleTimeCommands(cmd);

    return m_Graph.GetResource(m_LitColor);
  }
}
