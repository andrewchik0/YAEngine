#include "Render.h"

#include <imgui_impl_glfw.h>
#include <ImGui/imgui_impl_vulkan.h>

#include "BloomData.h"
#include "DebugMarker.h"
#include "ExposureData.h"
#include "ImageBarrier.h"
#include "Utils/Log.h"

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

    // 1. Depth prepass - fills depth buffer first (wins on overdraw)
    m_DepthPrepassIndex = m_Graph.AddPass({
      .name = "DepthPrepass",
      .depthOutput = m_MainDepth,
      .clearDepth = true,
      .depthOnly = true,
      .execute = [this](const RGExecuteContext& ctx) {
        auto* frame = static_cast<FrameContext*>(ctx.userData);
        DrawMeshesDepthOnly(ctx.cmd, m_Backend.GetCurrentFrameIndex(), *frame);
      }
    });

    // 2. G-buffer pass - writes geometry/material data only, no lighting
    m_GBufferPassIndex = m_Graph.AddPass({
      .name = "GBufferPass",
      .inputs = {},
      .colorOutputs = {m_GBuffer0, m_GBuffer1, m_MainVelocity},
      .depthOutput = m_MainDepth,
      .clearColor = true,
      .clearDepth = false,
      .execute = [this](const RGExecuteContext& ctx) {
        auto* frame = static_cast<FrameContext*>(ctx.userData);
        DrawMeshes(ctx.cmd, m_Backend.GetCurrentFrameIndex(), *frame);
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
        DrawQuad(ctx.cmd);
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
        DrawQuad(ctx.cmd);
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
        DrawQuad(ctx.cmd);
      }
    });

    // 5. Light Cull - tiled light culling (compute)
    m_LightCullPassIndex = m_Graph.AddPass({
      .name = "LightCull",
      .inputs = {m_MainDepth},
      .isCompute = true,
      .execute = [this](const RGExecuteContext& ctx) {
        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        auto& mainDepth = m_Graph.GetResource(m_MainDepth);

        // Write depth texture into light cull input descriptor set
        m_LightCullInputDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          mainDepth.GetView(), mainDepth.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        auto& pipeline = m_PSOCache.GetCompute(m_LightCullPipeline);
        pipeline.Bind(ctx.cmd);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_LightCullInputDescriptorSets[currentFrame].Get()}, 1);
        pipeline.BindDescriptorSets(ctx.cmd, {m_TileLightBuffer.GetDescriptorSet(currentFrame)}, 2);

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
        bufferBarrier.buffer = m_TileLightBuffer.GetBuffer(currentFrame);
        bufferBarrier.offset = 0;
        bufferBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(ctx.cmd,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          0, 0, nullptr, 1, &bufferBarrier, 0, nullptr);
      }
    });

    // 6. Deferred Lighting - fullscreen IBL + analytical lights from G-buffer
    m_DeferredLightingPassIndex = m_Graph.AddPass({
      .name = "DeferredLighting",
      .inputs = {m_GBuffer0, m_GBuffer1, m_MainDepth},
      .colorOutputs = {m_LitColor},
      .execute = [this](const RGExecuteContext& ctx) {
        auto currentFrame = m_Backend.GetCurrentFrameIndex();

        auto& gbuffer0 = m_Graph.GetResource(m_GBuffer0);
        auto& gbuffer1 = m_Graph.GetResource(m_GBuffer1);
        auto& mainDepth = m_Graph.GetResource(m_MainDepth);

        auto& pipeline = m_PSOCache.Get(m_DeferredLightingPipeline);
        pipeline.Bind(ctx.cmd);
        m_DeferredLightingDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          gbuffer0.GetView(), gbuffer0.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_DeferredLightingDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          gbuffer1.GetView(), gbuffer1.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_DeferredLightingDescriptorSets[currentFrame].WriteCombinedImageSampler(2,
          mainDepth.GetView(), mainDepth.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_DeferredLightingDescriptorSets[currentFrame].Get()}, 1);
        pipeline.BindDescriptorSets(ctx.cmd, {m_DeferredLightingLightDescriptorSets[currentFrame].Get()}, 2);
        pipeline.BindDescriptorSets(ctx.cmd, {m_IBLDescriptorSets[currentFrame].Get()}, 3);
        DrawQuad(ctx.cmd);
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

    // 7. SSR - Hi-Z accelerated screen-space reflections
    m_SSRPassIndex = m_Graph.AddPass({
      .name = "SSRPass",
      .inputs = {m_LitColor, m_MainDepth, m_GBuffer1, m_GBuffer0, m_HiZResource},
      .colorOutputs = {m_SSRColor},
      .execute = [this](const RGExecuteContext& ctx) {
        auto currentFrame = m_Backend.GetCurrentFrameIndex();

        auto& litColor = m_Graph.GetResource(m_LitColor);
        auto& mainDepth = m_Graph.GetResource(m_MainDepth);
        auto& gbuffer1 = m_Graph.GetResource(m_GBuffer1);
        auto& gbuffer0 = m_Graph.GetResource(m_GBuffer0);
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
          hiZ.GetView(), hiZ.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_SSRPassDescriptorSets[currentFrame].Get()}, 1);
        DrawQuad(ctx.cmd);
      }
    });

    // 8. Bloom - downsample/upsample chain (compute)
    m_BloomPassIndex = m_Graph.AddPass({
      .name = "BloomPass",
      .inputs = {m_SSRColor},
      .isCompute = true,
      .execute = [this](const RGExecuteContext& ctx) {
        if (!b_BloomEnabled) return;

        uint32_t baseW = m_Graph.GetExtent().width;
        uint32_t baseH = m_Graph.GetExtent().height;
        uint32_t mipCount = BLOOM_MIP_COUNT;

        // Transition bloom image to GENERAL for compute
        TransitionImageLayout(ctx.cmd, m_BloomImage.GetImage(),
          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
          VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount);

        // --- Downsample chain ---
        auto& downPipeline = m_PSOCache.GetCompute(m_BloomDownsamplePipeline);
        downPipeline.Bind(ctx.cmd);

        for (uint32_t mip = 0; mip < mipCount; mip++)
        {
          uint32_t srcW = (mip == 0) ? baseW : std::max(1u, baseW / 2 >> (mip - 1));
          uint32_t srcH = (mip == 0) ? baseH : std::max(1u, baseH / 2 >> (mip - 1));
          uint32_t dstW = std::max(1u, baseW / 2 >> mip);
          uint32_t dstH = std::max(1u, baseH / 2 >> mip);

          BloomPushConstants pc;
          pc.srcWidth = static_cast<int>(srcW);
          pc.srcHeight = static_cast<int>(srcH);
          pc.mipLevel = static_cast<int>(mip);
          pc.filterRadius = 0.0f;
          pc.threshold = m_BloomThreshold;
          pc.softKnee = m_BloomSoftKnee;

          downPipeline.BindDescriptorSets(ctx.cmd, {m_BloomDownsampleDescriptorSets[mip].Get()}, 0);
          downPipeline.PushConstants(ctx.cmd, &pc);
          downPipeline.Dispatch(ctx.cmd, (dstW + 15) / 16, (dstH + 15) / 16, 1);

          // Barrier between mips
          VkImageMemoryBarrier imgBarrier{};
          imgBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
          imgBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
          imgBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
          imgBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
          imgBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
          imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          imgBarrier.image = m_BloomImage.GetImage();
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

        // --- Upsample chain ---
        auto& upPipeline = m_PSOCache.GetCompute(m_BloomUpsamplePipeline);
        upPipeline.Bind(ctx.cmd);

        for (uint32_t i = 0; i < mipCount - 1; i++)
        {
          uint32_t dstMip = mipCount - 2 - i;
          uint32_t dstW = std::max(1u, baseW / 2 >> dstMip);
          uint32_t dstH = std::max(1u, baseH / 2 >> dstMip);

          BloomPushConstants pc;
          pc.srcWidth = static_cast<int>(dstW);
          pc.srcHeight = static_cast<int>(dstH);
          pc.mipLevel = static_cast<int>(dstMip);
          pc.filterRadius = 1.0f / float(dstW);

          upPipeline.BindDescriptorSets(ctx.cmd, {m_BloomUpsampleDescriptorSets[i].Get()}, 0);
          upPipeline.PushConstants(ctx.cmd, &pc);
          upPipeline.Dispatch(ctx.cmd, (dstW + 15) / 16, (dstH + 15) / 16, 1);

          // Barrier for next upsample step
          VkImageMemoryBarrier imgBarrier{};
          imgBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
          imgBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
          imgBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
          imgBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
          imgBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
          imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          imgBarrier.image = m_BloomImage.GetImage();
          imgBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
          imgBarrier.subresourceRange.baseMipLevel = dstMip;
          imgBarrier.subresourceRange.levelCount = 1;
          imgBarrier.subresourceRange.baseArrayLayer = 0;
          imgBarrier.subresourceRange.layerCount = 1;
          vkCmdPipelineBarrier(ctx.cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &imgBarrier);
        }

        // Final transition: bloom mip 0 -> SHADER_READ_ONLY for tonemap
        TransitionImageLayout(ctx.cmd, m_BloomImage.GetImage(),
          VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount);
        m_BloomImage.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
        DrawQuad(ctx.cmd);
      }
    });

    // Forward transparent pass - blends transparent meshes on top of TAA result
    // using existing MainDepth (LOAD, no write, LEQUAL) so depth match deferred.
    m_ForwardTransparentPassIndex = m_Graph.AddPass({
      .name = "ForwardTransparent",
      .inputs = {m_MainDepth},
      .colorOutputs = {m_TAAHistory0},
      .depthOutput = m_MainDepth,
      .clearColor = false,
      .clearDepth = false,
      .externalFramebuffer = true,
      .execute = [this](const RGExecuteContext& ctx) {
        auto* frame = static_cast<FrameContext*>(ctx.userData);
        DrawTransparent(ctx.cmd, m_Backend.GetCurrentFrameIndex(), *frame);
      }
    });

    // Auto Exposure - histogram build (compute)
    m_HistogramPassIndex = m_Graph.AddPass({
      .name = "HistogramBuild",
      .inputs = {m_TAAHistory0},
      .isCompute = true,
      .execute = [this](const RGExecuteContext& ctx) {
        if (!b_AutoExposureEnabled || m_GlobalFrameIndex < 4) return;

        auto currentFrame = m_Backend.GetCurrentFrameIndex();

        // Clear histogram buffer
        vkCmdFillBuffer(ctx.cmd, m_HistogramBuffer.Get(), 0,
          HISTOGRAM_BIN_COUNT * sizeof(uint32_t), 0);

        VkBufferMemoryBarrier clearBarrier {};
        clearBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        clearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        clearBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        clearBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        clearBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        clearBarrier.buffer = m_HistogramBuffer.Get();
        clearBarrier.offset = 0;
        clearBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(ctx.cmd,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          0, 0, nullptr, 1, &clearBarrier, 0, nullptr);

        // Write HDR texture into descriptor (input[0] set via SetPassInput for ping-pong)
        auto historyWriteHandle = m_TAAIndex == 0 ? m_TAAHistory0 : m_TAAHistory1;
        auto& historyCurrent = m_Graph.GetResource(historyWriteHandle);
        m_HistogramPassDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          historyCurrent.GetView(), historyCurrent.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        auto& pipeline = m_PSOCache.GetCompute(m_ExposureHistogramPipeline);
        pipeline.Bind(ctx.cmd);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_HistogramPassDescriptorSets[currentFrame].Get()}, 1);
        pipeline.BindDescriptorSets(ctx.cmd, {m_HistogramOutputDescriptorSet.Get()}, 2);

        uint32_t groupsX = (m_FrameUniformBuffer.uniforms.screenWidth + 15) / 16;
        uint32_t groupsY = (m_FrameUniformBuffer.uniforms.screenHeight + 15) / 16;
        pipeline.Dispatch(ctx.cmd, groupsX, groupsY, 1);

        // Barrier: histogram compute write -> compute read
        VkBufferMemoryBarrier histBarrier {};
        histBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        histBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        histBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        histBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        histBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        histBarrier.buffer = m_HistogramBuffer.Get();
        histBarrier.offset = 0;
        histBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(ctx.cmd,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          0, 0, nullptr, 1, &histBarrier, 0, nullptr);
      }
    });

    // Auto Exposure - adapt (compute)
    m_ExposureAdaptPassIndex = m_Graph.AddPass({
      .name = "ExposureAdapt",
      .isCompute = true,
      .execute = [this](const RGExecuteContext& ctx) {
        if (!b_AutoExposureEnabled || m_GlobalFrameIndex < 4)
        {
          float unity = 1.0f;
          m_ExposureBuffer.Update(0, &unity, sizeof(float));
          return;
        }

        auto currentFrame = m_Backend.GetCurrentFrameIndex();

        ExposureAdaptPushConstants pc;
        pc.lowPercentile = m_LowPercentile;
        pc.highPercentile = m_HighPercentile;
        pc.adaptSpeedUp = m_AdaptSpeedUp;
        pc.adaptSpeedDown = m_AdaptSpeedDown;
        pc.deltaTime = std::min(m_DeltaTime, 0.1f);

        auto& pipeline = m_PSOCache.GetCompute(m_ExposureAdaptPipeline);
        pipeline.Bind(ctx.cmd);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_ExposureAdaptDescriptorSets[currentFrame].Get()}, 1);
        pipeline.PushConstants(ctx.cmd, &pc);
        pipeline.Dispatch(ctx.cmd, 1, 1, 1);

        // Barrier: exposure compute write -> fragment read
        VkBufferMemoryBarrier expBarrier {};
        expBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        expBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        expBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        expBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        expBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        expBarrier.buffer = m_ExposureBuffer.Get();
        expBarrier.offset = 0;
        expBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(ctx.cmd,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          0, 0, nullptr, 1, &expBarrier, 0, nullptr);
      }
    });

#ifdef YA_EDITOR
    // Scene compose pass - tone mapping to offscreen texture for editor viewport
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
        pipeline.BindDescriptorSets(ctx.cmd, {m_ExposureReadDescriptorSets[currentFrame].Get()}, 2);
        pipeline.BindDescriptorSets(ctx.cmd, {m_BloomReadDescriptorSet.Get()}, 3);
        DrawQuad(ctx.cmd);
      }
    });

    // Gizmo scene pass - depth-tested gizmos (probe volumes) rendered against scene depth
    m_GizmoScenePassIndex = m_Graph.AddPass({
      .name = "GizmoScenePass",
      .colorOutputs = {m_SceneColor},
      .depthOutput = m_MainDepth,
      .clearColor = false,
      .clearDepth = false,
      .execute = [this](const RGExecuteContext& ctx) {
        if (!b_GizmosEnabled) return;
        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        m_GizmoRenderer.FlushDepthTested(ctx.cmd, m_FrameUniformBuffer.GetDescriptorSet(currentFrame));
      }
    });

    // Gizmo overlay pass - manipulators rendered on top (depth cleared so gizmos only test against each other)
    m_GizmoPassIndex = m_Graph.AddPass({
      .name = "GizmoPass",
      .colorOutputs = {m_SceneColor},
      .depthOutput = m_MainDepth,
      .clearColor = false,
      .clearDepth = true,
      .execute = [this](const RGExecuteContext& ctx) {
        if (!b_GizmosEnabled) return;
        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        m_GizmoRenderer.FlushOverlay(ctx.cmd, m_FrameUniformBuffer.GetDescriptorSet(currentFrame));
      }
    });

    // Swapchain pass - editor mode: ImGui only (scene displayed via viewport panel)
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
    // Swapchain pass - production mode: tone mapping + ImGui overlay
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
        pipeline.BindDescriptorSets(ctx.cmd, {m_ExposureReadDescriptorSets[currentFrame].Get()}, 2);
        pipeline.BindDescriptorSets(ctx.cmd, {m_BloomReadDescriptorSet.Get()}, 3);
        DrawQuad(ctx.cmd);

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

    YA_DEBUG_NAME(ctx.device, VK_OBJECT_TYPE_IMAGE,
      m_TAADepth.GetImage(), "TAA Depth");
    YA_DEBUG_NAME(ctx.device, VK_OBJECT_TYPE_IMAGE_VIEW,
      m_TAADepth.GetView(), "TAA Depth View");

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

      YA_DEBUG_NAMEF(ctx.device, VK_OBJECT_TYPE_FRAMEBUFFER,
        m_TAAFramebuffers[i], "TAA FB %u", i);
    }

    VkRenderPass transparentRP = m_Graph.GetPassRenderPass(m_ForwardTransparentPassIndex);

    for (uint32_t i = 0; i < 2; i++)
    {
      RGHandle historyHandle = (i == 0) ? m_TAAHistory0 : m_TAAHistory1;

      VkImageView views[2] = {
        m_Graph.GetResource(historyHandle).GetView(),
        m_Graph.GetResource(m_MainDepth).GetView()
      };

      VkFramebufferCreateInfo fbInfo{};
      fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fbInfo.renderPass = transparentRP;
      fbInfo.attachmentCount = 2;
      fbInfo.pAttachments = views;
      fbInfo.width = m_Graph.GetExtent().width;
      fbInfo.height = m_Graph.GetExtent().height;
      fbInfo.layers = 1;

      if (vkCreateFramebuffer(ctx.device, &fbInfo, nullptr, &m_TransparentFramebuffers[i]) != VK_SUCCESS)
      {
        YA_LOG_ERROR("Render", "Failed to create transparent framebuffer %d", i);
        throw std::runtime_error("Failed to create transparent framebuffer!");
      }

      YA_DEBUG_NAMEF(ctx.device, VK_OBJECT_TYPE_FRAMEBUFFER,
        m_TransparentFramebuffers[i], "Transparent FB %u", i);
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
