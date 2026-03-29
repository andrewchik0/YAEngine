#include "Render.h"

#include <imgui_impl_glfw.h>
#include <ImGui/imgui_impl_vulkan.h>

#include "ImageBarrier.h"
#include "Log.h"

namespace YAEngine
{
  void Render::SetupRenderGraph(uint32_t width, uint32_t height)
  {
    auto& ctx = m_Backend.GetContext();
    auto swapFormat = m_Backend.GetSwapChain().GetFormat();

    m_Graph.Init(ctx, {width, height});

    // G-buffer resources (16 bytes/pixel, down from 30)
    m_GBuffer0 = m_Graph.CreateResource({
      .name = "gbuffer0",
      .format = VK_FORMAT_R8G8B8A8_UNORM
    });
    m_GBuffer1 = m_Graph.CreateResource({
      .name = "gbuffer1",
      .format = VK_FORMAT_A2B10G10R10_UNORM_PACK32
    });
    m_MainDepth = m_Graph.CreateResource({
      .name = "mainDepth",
      .format = VK_FORMAT_D32_SFLOAT,
      .aspect = VK_IMAGE_ASPECT_DEPTH_BIT
    });
    m_MainVelocity = m_Graph.CreateResource({
      .name = "mainVelocity",
      .format = VK_FORMAT_R16G16_SFLOAT
    });

    // Deferred lighting output (replaces old mainColor)
    m_LitColor = m_Graph.CreateResource({
      .name = "litColor",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT
    });
    m_SSAOColor = m_Graph.CreateResource({
      .name = "ssaoColor",
      .format = VK_FORMAT_R8_UNORM
    });
    m_SSAOBlurIntermediate = m_Graph.CreateResource({
      .name = "ssaoBlurIntermediate",
      .format = VK_FORMAT_R8_UNORM
    });
    m_SSAOBlurred = m_Graph.CreateResource({
      .name = "ssaoBlurred",
      .format = VK_FORMAT_R8_UNORM
    });
    m_SSRColor = m_Graph.CreateResource({
      .name = "ssrColor",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT
    });
    m_TAAHistory0 = m_Graph.CreateResource({
      .name = "taaHistory0",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT,
      .additionalUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
    });
    m_TAAHistory1 = m_Graph.CreateResource({
      .name = "taaHistory1",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT,
      .additionalUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
    });

    uint32_t hizMipCount = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
    m_HiZResource = m_Graph.CreateResource({
      .name = "hiZ",
      .format = VK_FORMAT_R32_SFLOAT,
      .filter = VK_FILTER_NEAREST,
      .mipLevels = hizMipCount
    });

    // 1. Depth prepass — fills depth buffer first (wins on overdraw)
    m_DepthPrepassIndex = m_Graph.AddPass({
      .name = "DepthPrepass",
      .depthOutput = m_MainDepth,
      .clearDepth = true,
      .depthOnly = true,
      .execute = [this](const RGExecuteContext& ctx) {
        auto* frame = static_cast<FrameContext*>(ctx.userData);
        DrawMeshesDepthOnly(*frame);
      }
    });

    // 2. G-buffer pass — writes geometry/material data only, no lighting
    m_GBufferPassIndex = m_Graph.AddPass({
      .name = "GBufferPass",
      .inputs = {},
      .colorOutputs = {m_GBuffer0, m_GBuffer1, m_MainVelocity},
      .depthOutput = m_MainDepth,
      .clearColor = true,
      .clearDepth = false,
      .execute = [this](const RGExecuteContext& ctx) {
        auto* frame = static_cast<FrameContext*>(ctx.userData);
        DrawMeshes(*frame);
      }
    });

    m_SSAOPassIndex = m_Graph.AddPass({
      .name = "SSAOPass",
      .inputs = {m_MainDepth, m_GBuffer1},
      .colorOutputs = {m_SSAOColor},
      .execute = [this](const RGExecuteContext& ctx) {
        if (!b_SSAOEnabled) return;

        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        auto& mainDepth = m_Graph.GetResource(m_MainDepth);
        auto& gbuffer1 = m_Graph.GetResource(m_GBuffer1);

        auto& pipeline = m_PSOCache.Get(m_SSAOPipeline);
        pipeline.Bind(ctx.cmd);
        m_SSAOPassDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          mainDepth.GetView(), mainDepth.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SSAOPassDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          gbuffer1.GetView(), gbuffer1.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_SSAOPassDescriptorSets[currentFrame].Get()}, 1);
        DrawQuad();
      }
    });

    m_SSAOBlurHPassIndex = m_Graph.AddPass({
      .name = "SSAOBlurHPass",
      .inputs = {m_SSAOColor, m_MainDepth},
      .colorOutputs = {m_SSAOBlurIntermediate},
      .execute = [this](const RGExecuteContext& ctx) {
        if (!b_SSAOEnabled) return;

        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        auto& ssaoColor = m_Graph.GetResource(m_SSAOColor);
        auto& depth = m_Graph.GetResource(m_MainDepth);

        auto& pipeline = m_PSOCache.Get(m_SSAOBlurHPipeline);
        pipeline.Bind(ctx.cmd);
        m_SSAOBlurHPassDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          ssaoColor.GetView(), ssaoColor.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SSAOBlurHPassDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          depth.GetView(), depth.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_SSAOBlurHPassDescriptorSets[currentFrame].Get()}, 1);
        int direction = 0;
        pipeline.PushConstants(ctx.cmd, &direction);
        DrawQuad();
      }
    });

    m_SSAOBlurVPassIndex = m_Graph.AddPass({
      .name = "SSAOBlurVPass",
      .inputs = {m_SSAOBlurIntermediate, m_MainDepth},
      .colorOutputs = {m_SSAOBlurred},
      .execute = [this](const RGExecuteContext& ctx) {
        if (!b_SSAOEnabled) return;

        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        auto& intermediate = m_Graph.GetResource(m_SSAOBlurIntermediate);
        auto& depth = m_Graph.GetResource(m_MainDepth);

        auto& pipeline = m_PSOCache.Get(m_SSAOBlurVPipeline);
        pipeline.Bind(ctx.cmd);
        m_SSAOBlurVPassDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          intermediate.GetView(), intermediate.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SSAOBlurVPassDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          depth.GetView(), depth.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_SSAOBlurVPassDescriptorSets[currentFrame].Get()}, 1);
        int direction = 1;
        pipeline.PushConstants(ctx.cmd, &direction);
        DrawQuad();
      }
    });

    // 5. Deferred Lighting — fullscreen IBL computation from G-buffer
    m_DeferredLightingPassIndex = m_Graph.AddPass({
      .name = "DeferredLighting",
      .inputs = {m_GBuffer0, m_GBuffer1, m_MainDepth, m_SSAOBlurred},
      .colorOutputs = {m_LitColor},
      .execute = [this](const RGExecuteContext& ctx) {
        auto currentFrame = m_Backend.GetCurrentFrameIndex();

        auto& gbuffer0 = m_Graph.GetResource(m_GBuffer0);
        auto& gbuffer1 = m_Graph.GetResource(m_GBuffer1);
        auto& mainDepth = m_Graph.GetResource(m_MainDepth);
        auto& ssaoBlurred = m_Graph.GetResource(m_SSAOBlurred);

        auto& pipeline = m_PSOCache.Get(m_DeferredLightingPipeline);
        pipeline.Bind(ctx.cmd);
        m_DeferredLightingDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          gbuffer0.GetView(), gbuffer0.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_DeferredLightingDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          gbuffer1.GetView(), gbuffer1.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_DeferredLightingDescriptorSets[currentFrame].WriteCombinedImageSampler(2,
          mainDepth.GetView(), mainDepth.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_DeferredLightingDescriptorSets[currentFrame].WriteCombinedImageSampler(3,
          ssaoBlurred.GetView(), ssaoBlurred.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_DeferredLightingDescriptorSets[currentFrame].Get()}, 1);
        pipeline.BindDescriptorSets(ctx.cmd, {m_LightBuffer.GetDescriptorSet(currentFrame)}, 2);
        pipeline.BindDescriptorSets(ctx.cmd, {m_IBLDescriptorSet.Get()}, 3);
        DrawQuad();
      }
    });

    // 6. Hi-Z mip chain generation (compute)
    m_HiZPassIndex = m_Graph.AddPass({
      .name = "HiZPass",
      .inputs = {m_MainDepth},
      .storageOutputs = {m_HiZResource},
      .isCompute = true,
      .execute = [this](const RGExecuteContext& ctx) {
        uint32_t mipCount = m_Graph.GetResourceDesc(m_HiZResource).mipLevels;
        uint32_t w = m_Graph.GetExtent().width;
        uint32_t h = m_Graph.GetExtent().height;

        auto& hizPipeline = m_PSOCache.GetCompute(m_HiZPipeline);
        hizPipeline.Bind(ctx.cmd);

        for (uint32_t mip = 0; mip < mipCount; mip++)
        {
          uint32_t dstW = std::max(1u, w >> mip);
          uint32_t dstH = std::max(1u, h >> mip);

          struct { int srcMip; int dstWidth; int dstHeight; } pc;
          pc.srcMip = (mip == 0) ? -1 : static_cast<int>(mip - 1);
          pc.dstWidth = static_cast<int>(dstW);
          pc.dstHeight = static_cast<int>(dstH);

          hizPipeline.BindDescriptorSets(ctx.cmd, {m_HiZDescriptorSets[mip].Get()}, 0);
          hizPipeline.PushConstants(ctx.cmd, &pc);
          hizPipeline.Dispatch(ctx.cmd, (dstW + 15) / 16, (dstH + 15) / 16, 1);

          if (mip < mipCount - 1)
          {
            VkImageMemoryBarrier imgBarrier{};
            imgBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imgBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            imgBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            imgBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            imgBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imgBarrier.image = m_Graph.GetResource(m_HiZResource).GetImage();
            imgBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imgBarrier.subresourceRange.baseMipLevel = mip;
            imgBarrier.subresourceRange.levelCount = 1;
            imgBarrier.subresourceRange.baseArrayLayer = 0;
            imgBarrier.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(ctx.cmd,
              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
              0, 0, nullptr, 0, nullptr, 1, &imgBarrier);
          }
        }
      }
    });

    // 7. SSR — Hi-Z accelerated screen-space reflections
    m_SSRPassIndex = m_Graph.AddPass({
      .name = "SSRPass",
      .inputs = {m_LitColor, m_MainDepth, m_GBuffer1, m_GBuffer0, m_SSAOBlurred, m_HiZResource},
      .colorOutputs = {m_SSRColor},
      .execute = [this](const RGExecuteContext& ctx) {
        auto currentFrame = m_Backend.GetCurrentFrameIndex();

        auto& litColor = m_Graph.GetResource(m_LitColor);
        auto& mainDepth = m_Graph.GetResource(m_MainDepth);
        auto& gbuffer1 = m_Graph.GetResource(m_GBuffer1);
        auto& gbuffer0 = m_Graph.GetResource(m_GBuffer0);
        auto& ssaoBlurred = m_Graph.GetResource(m_SSAOBlurred);
        auto& hiZ = m_Graph.GetResource(m_HiZResource);

        auto& pipeline = m_PSOCache.Get(m_SSRPipeline);
        pipeline.Bind(ctx.cmd);
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          litColor.GetView(), litColor.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          mainDepth.GetView(), mainDepth.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(2,
          gbuffer1.GetView(), gbuffer1.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(3,
          gbuffer0.GetView(), gbuffer0.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(4,
          ssaoBlurred.GetView(), ssaoBlurred.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(5,
          hiZ.GetView(), hiZ.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_SSRPassDescriptorSets[currentFrame].Get()}, 1);
        DrawQuad();
      }
    });

    m_TAAPassIndex = m_Graph.AddPass({
      .name = "TAAPass",
      .inputs = {m_SSRColor, m_TAAHistory1, m_MainVelocity},
      .colorOutputs = {m_TAAHistory0},
      .externalFramebuffer = true,
      .execute = [this](const RGExecuteContext& ctx) {
        auto& ssrColor = m_Graph.GetResource(m_SSRColor);
        auto historyReadHandle = m_TAAIndex == 0 ? m_TAAHistory1 : m_TAAHistory0;
        auto& historyPrev = m_Graph.GetResource(historyReadHandle);
        auto& velocity = m_Graph.GetResource(m_MainVelocity);

        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        auto& pipeline = m_PSOCache.Get(m_TAAPipeline);
        pipeline.Bind(ctx.cmd);
        m_TAADescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          ssrColor.GetView(), ssrColor.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_TAADescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          historyPrev.GetView(), historyPrev.GetSampler(), historyPrev.GetLayout());
        m_TAADescriptorSets[currentFrame].WriteCombinedImageSampler(2,
          velocity.GetView(), velocity.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_TAADescriptorSets[currentFrame].Get()}, 1);
        DrawQuad();
      }
    });

#ifdef YA_EDITOR
    // Scene compose pass — tone mapping to offscreen texture for editor viewport
    m_SceneColor = m_Graph.CreateResource({
      .name = "sceneColor",
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .additionalUsage = VK_IMAGE_USAGE_SAMPLED_BIT
    });

    m_SceneComposePassIndex = m_Graph.AddPass({
      .name = "SceneComposePass",
      .inputs = {m_TAAHistory0, m_SSAOBlurred, m_GBuffer0, m_GBuffer1},
      .colorOutputs = {m_SceneColor},
      .execute = [this](const RGExecuteContext& ctx) {
        auto historyWriteHandle = m_TAAIndex == 0 ? m_TAAHistory0 : m_TAAHistory1;
        auto& historyCurrent = m_Graph.GetResource(historyWriteHandle);
        auto& ssaoBlurred = m_Graph.GetResource(m_SSAOBlurred);
        auto& gbuffer0 = m_Graph.GetResource(m_GBuffer0);
        auto& gbuffer1 = m_Graph.GetResource(m_GBuffer1);

        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        auto& pipeline = m_PSOCache.Get(m_QuadPipeline);
        pipeline.Bind(ctx.cmd);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          historyCurrent.GetView(), historyCurrent.GetSampler(), historyCurrent.GetLayout());
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          ssaoBlurred.GetView(), ssaoBlurred.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(2,
          gbuffer0.GetView(), gbuffer0.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(3,
          gbuffer1.GetView(), gbuffer1.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_SwapChainDescriptorSets[currentFrame].Get()}, 1);
        DrawQuad();
      }
    });

    // Swapchain pass — editor mode: ImGui only (scene displayed via viewport panel)
    m_SwapchainPassIndex = m_Graph.AddPass({
      .name = "SwapchainPass",
      .inputs = {m_SceneColor},
      .colorOutputs = {},
      .externalFramebuffer = true,
      .externalFormat = swapFormat,
      .finalColorLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .execute = [this](const RGExecuteContext& ctx) {
        auto* frame = static_cast<FrameContext*>(ctx.userData);

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        if (frame->renderUI) frame->renderUI(frame->renderUIData);
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), ctx.cmd);
      }
    });
#else
    // Swapchain pass — production mode: tone mapping + ImGui overlay
    m_SwapchainPassIndex = m_Graph.AddPass({
      .name = "SwapchainPass",
      .inputs = {m_TAAHistory0, m_SSAOBlurred, m_GBuffer0, m_GBuffer1},
      .colorOutputs = {},
      .externalFramebuffer = true,
      .externalFormat = swapFormat,
      .finalColorLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .execute = [this](const RGExecuteContext& ctx) {
        auto* frame = static_cast<FrameContext*>(ctx.userData);

        auto historyWriteHandle = m_TAAIndex == 0 ? m_TAAHistory0 : m_TAAHistory1;
        auto& historyCurrent = m_Graph.GetResource(historyWriteHandle);
        auto& ssaoBlurred = m_Graph.GetResource(m_SSAOBlurred);
        auto& gbuffer0 = m_Graph.GetResource(m_GBuffer0);
        auto& gbuffer1 = m_Graph.GetResource(m_GBuffer1);

        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        auto& pipeline = m_PSOCache.Get(m_QuadPipeline);
        pipeline.Bind(ctx.cmd);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          historyCurrent.GetView(), historyCurrent.GetSampler(), historyCurrent.GetLayout());
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          ssaoBlurred.GetView(), ssaoBlurred.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(2,
          gbuffer0.GetView(), gbuffer0.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(3,
          gbuffer1.GetView(), gbuffer1.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_SwapChainDescriptorSets[currentFrame].Get()}, 1);
        DrawQuad();

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        if (frame->renderUI) frame->renderUI(frame->renderUIData);
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), ctx.cmd);
      }
    });
#endif

    m_Graph.Compile();
  }

  void Render::CreateTAAFramebuffers()
  {
    auto& ctx = m_Backend.GetContext();

    // Create depth image shared by both TAA framebuffers
    ImageDesc depthDesc;
    depthDesc.width = m_Graph.GetExtent().width;
    depthDesc.height = m_Graph.GetExtent().height;
    depthDesc.format = VK_FORMAT_D32_SFLOAT;
    depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthDesc.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    m_TAADepth.Init(ctx, depthDesc);

    VkRenderPass taaRP = m_Graph.GetPassRenderPass(m_TAAPassIndex);

    for (uint32_t i = 0; i < 2; i++)
    {
      RGHandle historyHandle = (i == 0) ? m_TAAHistory0 : m_TAAHistory1;

      VkImageView views[2] = {
        m_Graph.GetResource(historyHandle).GetView(),
        m_TAADepth.GetView()
      };

      VkFramebufferCreateInfo fbInfo{};
      fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fbInfo.renderPass = taaRP;
      fbInfo.attachmentCount = 2;
      fbInfo.pAttachments = views;
      fbInfo.width = m_Graph.GetExtent().width;
      fbInfo.height = m_Graph.GetExtent().height;
      fbInfo.layers = 1;

      if (vkCreateFramebuffer(ctx.device, &fbInfo, nullptr, &m_TAAFramebuffers[i]) != VK_SUCCESS)
      {
        YA_LOG_ERROR("Render", "Failed to create TAA framebuffer %d", i);
        throw std::runtime_error("Failed to create TAA framebuffer!");
      }
    }
  }

  void Render::ClearHistoryBuffers()
  {
    VkRenderPass taaRP = m_Graph.GetPassRenderPass(m_TAAPassIndex);
    auto extent = m_Graph.GetExtent();

    auto cmd = m_Backend.GetCommandBuffer().BeginSingleTimeCommands();

    // Transition depth once
    TransitionImageLayout(cmd, m_TAADepth.GetImage(),
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      VK_IMAGE_ASPECT_DEPTH_BIT);

    for (uint32_t i = 0; i < 2; i++)
    {
      auto handle = (i == 0) ? m_TAAHistory0 : m_TAAHistory1;
      auto& image = m_Graph.GetResource(handle);

      // Render pass with loadOp=CLEAR will clear the contents
      // initialLayout=UNDEFINED is fine here since we don't need previous contents
      VkClearValue clearValues[2] = {};
      clearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
      clearValues[1].depthStencil = {1.0f, 0};

      VkRenderPassBeginInfo rpBegin{};
      rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      rpBegin.renderPass = taaRP;
      rpBegin.framebuffer = m_TAAFramebuffers[i];
      rpBegin.renderArea.extent = extent;
      rpBegin.clearValueCount = 2;
      rpBegin.pClearValues = clearValues;

      vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdEndRenderPass(cmd);

      // After render pass with finalLayout=COLOR_ATTACHMENT_OPTIMAL,
      // transition to SHADER_READ_ONLY for first frame's use
      TransitionImageLayout(cmd, image.GetImage(),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

      image.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      m_Graph.SetResourceLayout(handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    m_Backend.GetCommandBuffer().EndSingleTimeCommands(cmd);
  }
}
