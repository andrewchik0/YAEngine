#include "Render.h"

#include <imgui_impl_glfw.h>
#include <ImGui/imgui_impl_vulkan.h>

#include "Application.h"
#include "ImageBarrier.h"
#include "VulkanVertexBuffer.h"
#include "Log.h"
#include "SSAOKernel.h"

#include "Utils/Utils.h"

#include <random>

namespace YAEngine
{
  void Render::SetupRenderGraph(uint32_t width, uint32_t height)
  {
    auto& ctx = m_Backend.GetContext();
    auto swapFormat = m_Backend.GetSwapChain().GetFormat();

    m_Graph.Init(ctx, {width, height});

    // --- Resources ---
    m_MainColor = m_Graph.CreateResource({
      .name = "mainColor",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT
    });
    m_MainNormals = m_Graph.CreateResource({
      .name = "mainNormals",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT
    });
    m_MainDepth = m_Graph.CreateResource({
      .name = "mainDepth",
      .format = VK_FORMAT_D32_SFLOAT,
      .aspect = VK_IMAGE_ASPECT_DEPTH_BIT
    });
    m_MainMaterial = m_Graph.CreateResource({
      .name = "mainMaterial",
      .format = VK_FORMAT_R8G8_UNORM
    });
    m_MainAlbedo = m_Graph.CreateResource({
      .name = "mainAlbedo",
      .format = VK_FORMAT_R8G8B8A8_UNORM
    });
    m_MainVelocity = m_Graph.CreateResource({
      .name = "mainVelocity",
      .format = VK_FORMAT_R16G16_SFLOAT
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

    // --- Passes ---

    // 1. Depth prepass — fills depth buffer first (wins on overdraw)
    m_DepthPrepassIndex = m_Graph.AddPass({
      .name = "DepthPrepass",
      .depthOutput = m_MainDepth,
      .clearDepth = true,
      .depthOnly = true,
      .execute = [this](const RGExecuteContext& ctx) {
        auto* app = static_cast<Application*>(ctx.userData);
        DrawMeshesDepthOnly(app);
      }
    });

    // 2. Main pass — forward PBR, reuses depth from prepass (EQUAL test, no depth write)
    m_MainPassIndex = m_Graph.AddPass({
      .name = "MainPass",
      .inputs = {},
      .colorOutputs = {m_MainColor, m_MainNormals, m_MainMaterial, m_MainAlbedo, m_MainVelocity},
      .depthOutput = m_MainDepth,
      .clearColor = true,
      .clearDepth = false,
      .execute = [this](const RGExecuteContext& ctx) {
        auto* app = static_cast<Application*>(ctx.userData);
        auto currentFrame = m_Backend.GetCurrentFrameIndex();

        m_ForwardPipeline.Bind(ctx.cmd);
        m_ForwardPipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        m_ForwardPipelineDoubleSided.Bind(ctx.cmd);
        m_ForwardPipelineDoubleSided.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);

        DrawMeshes(app);
      }
    });

    m_SSAOPassIndex = m_Graph.AddPass({
      .name = "SSAOPass",
      .inputs = {m_MainDepth, m_MainNormals},
      .colorOutputs = {m_SSAOColor},
      .execute = [this](const RGExecuteContext& ctx) {
        if (!b_SSAOEnabled) return;

        auto currentFrame = m_Backend.GetCurrentFrameIndex();

        auto& mainDepth = m_Graph.GetResource(m_MainDepth);
        auto& mainNormals = m_Graph.GetResource(m_MainNormals);

        m_SSAOPipeline.Bind(ctx.cmd);
        m_SSAOPassDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          mainDepth.GetView(), mainDepth.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SSAOPassDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          mainNormals.GetView(), mainNormals.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        m_SSAOPipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        m_SSAOPipeline.BindDescriptorSets(ctx.cmd, {m_SSAOPassDescriptorSets[currentFrame].Get()}, 1);
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

        m_SSAOBlurHPipeline.Bind(ctx.cmd);
        m_SSAOBlurHPassDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          ssaoColor.GetView(), ssaoColor.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SSAOBlurHPassDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          depth.GetView(), depth.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        m_SSAOBlurHPipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        m_SSAOBlurHPipeline.BindDescriptorSets(ctx.cmd, {m_SSAOBlurHPassDescriptorSets[currentFrame].Get()}, 1);
        int direction = 0;
        m_SSAOBlurHPipeline.PushConstants(ctx.cmd, &direction);
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

        m_SSAOBlurVPipeline.Bind(ctx.cmd);
        m_SSAOBlurVPassDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          intermediate.GetView(), intermediate.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SSAOBlurVPassDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          depth.GetView(), depth.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        m_SSAOBlurVPipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        m_SSAOBlurVPipeline.BindDescriptorSets(ctx.cmd, {m_SSAOBlurVPassDescriptorSets[currentFrame].Get()}, 1);
        int direction = 1;
        m_SSAOBlurVPipeline.PushConstants(ctx.cmd, &direction);
        DrawQuad();
      }
    });

    // 5. Hi-Z mip chain generation (compute)
    m_HiZPassIndex = m_Graph.AddPass({
      .name = "HiZPass",
      .inputs = {m_MainDepth},
      .storageOutputs = {m_HiZResource},
      .isCompute = true,
      .execute = [this](const RGExecuteContext& ctx) {
        uint32_t mipCount = m_Graph.GetResourceDesc(m_HiZResource).mipLevels;
        uint32_t w = m_Graph.GetExtent().width;
        uint32_t h = m_Graph.GetExtent().height;

        m_HiZPipeline.Bind(ctx.cmd);

        for (uint32_t mip = 0; mip < mipCount; mip++)
        {
          uint32_t dstW = std::max(1u, w >> mip);
          uint32_t dstH = std::max(1u, h >> mip);

          struct { int srcMip; int dstWidth; int dstHeight; } pc;
          pc.srcMip = (mip == 0) ? -1 : static_cast<int>(mip - 1);
          pc.dstWidth = static_cast<int>(dstW);
          pc.dstHeight = static_cast<int>(dstH);

          m_HiZPipeline.BindDescriptorSets(ctx.cmd, {m_HiZDescriptorSets[mip].Get()}, 0);
          m_HiZPipeline.PushConstants(ctx.cmd, &pc);
          m_HiZPipeline.Dispatch(ctx.cmd, (dstW + 15) / 16, (dstH + 15) / 16, 1);

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

    // 6. SSR — Hi-Z accelerated screen-space reflections
    m_SSRPassIndex = m_Graph.AddPass({
      .name = "SSRPass",
      .inputs = {m_MainColor, m_MainDepth, m_MainNormals, m_MainMaterial, m_MainAlbedo, m_SSAOBlurred, m_HiZResource},
      .colorOutputs = {m_SSRColor},
      .execute = [this](const RGExecuteContext& ctx) {
        auto currentFrame = m_Backend.GetCurrentFrameIndex();

        auto& mainColor = m_Graph.GetResource(m_MainColor);
        auto& mainDepth = m_Graph.GetResource(m_MainDepth);
        auto& mainNormals = m_Graph.GetResource(m_MainNormals);
        auto& mainMaterial = m_Graph.GetResource(m_MainMaterial);
        auto& mainAlbedo = m_Graph.GetResource(m_MainAlbedo);
        auto& ssaoBlurred = m_Graph.GetResource(m_SSAOBlurred);
        auto& hiZ = m_Graph.GetResource(m_HiZResource);

        m_SSRPipeline.Bind(ctx.cmd);
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          mainColor.GetView(), mainColor.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          mainDepth.GetView(), mainDepth.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(2,
          mainNormals.GetView(), mainNormals.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(3,
          mainMaterial.GetView(), mainMaterial.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(4,
          mainAlbedo.GetView(), mainAlbedo.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(5,
          ssaoBlurred.GetView(), ssaoBlurred.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(6,
          hiZ.GetView(), hiZ.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        m_SSRPipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        m_SSRPipeline.BindDescriptorSets(ctx.cmd, {m_SSRPassDescriptorSets[currentFrame].Get()}, 1);
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
        m_TAAPipeline.Bind(ctx.cmd);
        m_TAADescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          ssrColor.GetView(), ssrColor.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_TAADescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          historyPrev.GetView(), historyPrev.GetSampler(), historyPrev.GetLayout());
        m_TAADescriptorSets[currentFrame].WriteCombinedImageSampler(2,
          velocity.GetView(), velocity.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_TAAPipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        m_TAAPipeline.BindDescriptorSets(ctx.cmd, {m_TAADescriptorSets[currentFrame].Get()}, 1);
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
      .inputs = {m_TAAHistory0, m_SSAOBlurred, m_MainAlbedo, m_MainNormals, m_MainMaterial},
      .colorOutputs = {m_SceneColor},
      .execute = [this](const RGExecuteContext& ctx) {
        auto historyWriteHandle = m_TAAIndex == 0 ? m_TAAHistory0 : m_TAAHistory1;
        auto& historyCurrent = m_Graph.GetResource(historyWriteHandle);
        auto& ssaoBlurred = m_Graph.GetResource(m_SSAOBlurred);
        auto& mainAlbedo = m_Graph.GetResource(m_MainAlbedo);
        auto& mainNormals = m_Graph.GetResource(m_MainNormals);
        auto& mainMaterial = m_Graph.GetResource(m_MainMaterial);

        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        m_QuadPipeline.Bind(ctx.cmd);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          historyCurrent.GetView(), historyCurrent.GetSampler(), historyCurrent.GetLayout());
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          ssaoBlurred.GetView(), ssaoBlurred.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(2,
          mainAlbedo.GetView(), mainAlbedo.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(3,
          mainNormals.GetView(), mainNormals.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(4,
          mainMaterial.GetView(), mainMaterial.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_QuadPipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        m_QuadPipeline.BindDescriptorSets(ctx.cmd, {m_SwapChainDescriptorSets[currentFrame].Get()}, 1);
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
        auto* app = static_cast<Application*>(ctx.userData);

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        app->RenderUI();
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), ctx.cmd);
      }
    });
#else
    // Swapchain pass — production mode: tone mapping + ImGui overlay
    m_SwapchainPassIndex = m_Graph.AddPass({
      .name = "SwapchainPass",
      .inputs = {m_TAAHistory0, m_SSAOBlurred, m_MainAlbedo, m_MainNormals, m_MainMaterial},
      .colorOutputs = {},
      .externalFramebuffer = true,
      .externalFormat = swapFormat,
      .finalColorLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .execute = [this](const RGExecuteContext& ctx) {
        auto* app = static_cast<Application*>(ctx.userData);

        auto historyWriteHandle = m_TAAIndex == 0 ? m_TAAHistory0 : m_TAAHistory1;
        auto& historyCurrent = m_Graph.GetResource(historyWriteHandle);
        auto& ssaoBlurred = m_Graph.GetResource(m_SSAOBlurred);
        auto& mainAlbedo = m_Graph.GetResource(m_MainAlbedo);
        auto& mainNormals = m_Graph.GetResource(m_MainNormals);
        auto& mainMaterial = m_Graph.GetResource(m_MainMaterial);

        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        m_QuadPipeline.Bind(ctx.cmd);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          historyCurrent.GetView(), historyCurrent.GetSampler(), historyCurrent.GetLayout());
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          ssaoBlurred.GetView(), ssaoBlurred.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(2,
          mainAlbedo.GetView(), mainAlbedo.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(3,
          mainNormals.GetView(), mainNormals.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(4,
          mainMaterial.GetView(), mainMaterial.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_QuadPipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        m_QuadPipeline.BindDescriptorSets(ctx.cmd, {m_SwapChainDescriptorSets[currentFrame].Get()}, 1);
        DrawQuad();

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        app->RenderUI();
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

  void Render::Init(GLFWwindow* window, const RenderSpecs &specs)
  {
    m_Backend.Init(window, specs);
    auto& ctx = m_Backend.GetContext();

    int width, height;
    glfwGetWindowSize(window, &width, &height);

    uint32_t whitePixel = 0xFFFFFFFF;
    m_NoneTexture.Load(ctx, &whitePixel, 1, 1, 4, VK_FORMAT_R8G8B8A8_SRGB);

    m_DefaultMaterial.Init(ctx, m_NoneTexture);
    m_FrameUniformBuffer.Init(ctx);

    // Generate SSAO noise texture (4x4 random tangent-space rotations)
    {
      std::mt19937 rng(42);
      std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

      // 16 pixels, R32G32B32A32_SFLOAT (16 bytes per pixel)
      std::array<glm::vec4, 16> noiseData;
      for (auto& v : noiseData)
      {
        v = glm::vec4(dist(rng), dist(rng), 0.0f, 0.0f);
        v = glm::vec4(glm::normalize(glm::vec2(v.x, v.y)), 0.0f, 0.0f);
      }

      // Convert to half-float manually via uint16 — use R16G16B16A16_SFLOAT
      // VulkanTexture::Load expects raw data with pixelSize per pixel
      // Use R32G32B32A32_SFLOAT for simplicity (16 bytes per pixel)
      m_SSAONoise.Load(ctx, noiseData.data(), 4, 4, 16, VK_FORMAT_R32G32B32A32_SFLOAT);
    }

    // Generate SSAO hemisphere kernel
    {
      std::mt19937 rng(0);
      std::uniform_real_distribution<float> dist(0.0f, 1.0f);

      SSAOKernel kernelData;

      for (int i = 0; i < SSAO_KERNEL_SIZE; i++)
      {
        glm::vec3 sample(
          dist(rng) * 2.0f - 1.0f,
          dist(rng) * 2.0f - 1.0f,
          dist(rng)
        );
        sample = glm::normalize(sample);
        sample *= dist(rng);

        // Accelerating interpolation — bias samples closer to origin
        float scale = float(i) / float(SSAO_KERNEL_SIZE);
        scale = 0.1f + scale * scale * 0.9f;
        sample *= scale;

        kernelData.samples[i] = glm::vec4(sample, 0.0f);
      }

      m_SSAOKernelUBO.Create(ctx, sizeof(SSAOKernel));
      m_SSAOKernelUBO.Update(kernelData);
    }

    // Setup and compile render graph
    SetupRenderGraph(width, height);

    // Create TAA external framebuffers (ping-pong)
    CreateTAAFramebuffers();

    // Create swapchain framebuffers using graph's render pass
    m_Backend.GetSwapChain().CreateFrameBuffers(
      m_Graph.GetPassRenderPass(m_SwapchainPassIndex));

    // Clear TAA history buffers
    ClearHistoryBuffers();

    // Init pipelines using graph's render passes
    InitPipelines();

    // Create Hi-Z per-mip views and descriptor sets
    CreateHiZResources();

    m_Backend.InitImGui(window, m_Graph.GetPassRenderPass(m_SwapchainPassIndex));

#ifdef YA_EDITOR
    CreateSceneImGuiDescriptor();
    m_ViewportWidth = width;
    m_ViewportHeight = height;
    m_PendingViewportWidth = width;
    m_PendingViewportHeight = height;
#endif

    m_CubicResources.Init(ctx);
    m_SkyBox.Init(ctx, m_Graph.GetPassRenderPass(m_MainPassIndex));

    vkDeviceWaitIdle(ctx.device);
  }

  void Render::WaitIdle()
  {
    vkDeviceWaitIdle(m_Backend.GetContext().device);
  }

  void Render::Destroy()
  {
    vkDeviceWaitIdle(m_Backend.GetContext().device);

    auto& ctx = m_Backend.GetContext();

#ifdef YA_EDITOR
    DestroySceneImGuiDescriptor();
#endif

    m_SkyBox.Destroy();
    m_CubicResources.Destroy(ctx);
    m_NoneTexture.Destroy(ctx);

    // Destroy Hi-Z resources
    DestroyHiZResources();
    m_HiZPipeline.Destroy();

    // Destroy TAA external framebuffers
    for (auto& fb : m_TAAFramebuffers)
    {
      if (fb != VK_NULL_HANDLE)
      {
        vkDestroyFramebuffer(ctx.device, fb, nullptr);
        fb = VK_NULL_HANDLE;
      }
    }
    m_TAADepth.Destroy(ctx);

    for (auto& set : m_SwapChainDescriptorSets)
    {
      set.Destroy();
    }
    for (auto& set : m_TAADescriptorSets)
    {
      set.Destroy();
    }
    for (auto& set : m_SSRPassDescriptorSets)
    {
      set.Destroy();
    }
    m_SSRPipeline.Destroy();
    for (auto& set : m_SSAOPassDescriptorSets)
    {
      set.Destroy();
    }
    for (auto& set : m_SSAOBlurHPassDescriptorSets)
    {
      set.Destroy();
    }
    for (auto& set : m_SSAOBlurVPassDescriptorSets)
    {
      set.Destroy();
    }
    m_SSAOPipeline.Destroy();
    m_SSAOBlurHPipeline.Destroy();
    m_SSAOBlurVPipeline.Destroy();
    m_SSAONoise.Destroy(ctx);
    m_SSAOKernelUBO.Destroy(ctx);
    m_DepthPrepassPipeline.Destroy();
    m_DepthPrepassPipelineDoubleSided.Destroy();
    m_DepthPrepassPipelineInstanced.Destroy();
    m_DepthPrepassPipelineDoubleSidedInstanced.Destroy();
    m_ForwardPipeline.Destroy();
    m_ForwardPipelineDoubleSided.Destroy();
    m_ForwardPipelineInstanced.Destroy();
    m_ForwardPipelineDoubleSidedInstanced.Destroy();
    m_ForwardPipelineNoShading.Destroy();
    m_InstanceDescriptorSet.Destroy();
    m_InstanceBuffer.Destroy(ctx);
    m_QuadPipeline.Destroy();
    m_TAAPipeline.Destroy();
    m_DefaultMaterial.Destroy(ctx);
    m_FrameUniformBuffer.Destroy(ctx);

    m_Graph.Destroy();
    m_Backend.Destroy();
  }

  void Render::Resize()
  {
    b_Resized = false;

    // Recreate swapchain first to get actual surface dimensions
    m_Backend.GetSwapChain().Recreate(
      m_Graph.GetPassRenderPass(m_SwapchainPassIndex));

#ifdef YA_EDITOR
    // In editor mode, graph extent = viewport size (independent of window size).
    // Only swapchain is recreated here; viewport resize handled by ResizeViewport().
#else
    auto& ctx = m_Backend.GetContext();
    auto actualExtent = m_Backend.GetSwapChain().GetExt();

    // Wait for all GPU work to complete before destroying resources
    vkDeviceWaitIdle(ctx.device);

    // Destroy Hi-Z per-mip views before graph resize destroys the image
    DestroyHiZResources();

    // Resize graph (recreates managed resources and non-external framebuffers)
    m_Graph.Resize(actualExtent);

    // Recreate Hi-Z per-mip views and descriptor sets
    CreateHiZResources();

    // Recreate TAA external framebuffers
    for (auto& fb : m_TAAFramebuffers)
    {
      if (fb != VK_NULL_HANDLE)
      {
        vkDestroyFramebuffer(ctx.device, fb, nullptr);
        fb = VK_NULL_HANDLE;
      }
    }
    m_TAADepth.Destroy(ctx);
    CreateTAAFramebuffers();

    // Clear history buffers
    ClearHistoryBuffers();
#endif
  }

#ifdef YA_EDITOR
  void Render::CreateSceneImGuiDescriptor()
  {
    auto& sceneImage = m_Graph.GetResource(m_SceneColor);
    m_SceneImGuiDescriptor = ImGui_ImplVulkan_AddTexture(
      sceneImage.GetSampler(),
      sceneImage.GetView(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }

  void Render::DestroySceneImGuiDescriptor()
  {
    if (m_SceneImGuiDescriptor != VK_NULL_HANDLE)
    {
      ImGui_ImplVulkan_RemoveTexture(m_SceneImGuiDescriptor);
      m_SceneImGuiDescriptor = VK_NULL_HANDLE;
    }
  }

  void Render::RequestViewportResize(uint32_t w, uint32_t h)
  {
    m_PendingViewportWidth = w;
    m_PendingViewportHeight = h;
  }

  void Render::ResizeViewport()
  {
    auto& ctx = m_Backend.GetContext();

    // Wait for all GPU work to complete before destroying resources
    vkDeviceWaitIdle(ctx.device);

    uint32_t w = m_PendingViewportWidth;
    uint32_t h = m_PendingViewportHeight;

    // Update HiZ mip count for new dimensions
    uint32_t hizMipCount = static_cast<uint32_t>(std::floor(std::log2(std::max(w, h)))) + 1;
    m_Graph.SetResourceMipLevels(m_HiZResource, hizMipCount);

    DestroyHiZResources();
    DestroySceneImGuiDescriptor();

    m_Graph.Resize({w, h});

    CreateSceneImGuiDescriptor();
    CreateHiZResources();

    // Recreate TAA external framebuffers
    for (auto& fb : m_TAAFramebuffers)
    {
      if (fb != VK_NULL_HANDLE)
      {
        vkDestroyFramebuffer(ctx.device, fb, nullptr);
        fb = VK_NULL_HANDLE;
      }
    }
    m_TAADepth.Destroy(ctx);
    CreateTAAFramebuffers();
    ClearHistoryBuffers();

    m_ViewportWidth = w;
    m_ViewportHeight = h;
  }
#endif

  void Render::Draw(Application *app)
  {
#ifdef YA_EDITOR
    // Handle deferred viewport resize BEFORE acquiring the frame —
    // no command buffer is recording at this point, safe to wait for GPU and recreate resources
    if (m_PendingViewportWidth != m_ViewportWidth || m_PendingViewportHeight != m_ViewportHeight)
    {
      if (m_PendingViewportWidth > 0 && m_PendingViewportHeight > 0)
      {
        ResizeViewport();

        // Update aspect ratio for all cameras so switching doesn't produce distortion
        for (auto [entity, cam] : app->GetScene().GetView<CameraComponent>().each())
          cam.Resize(float(m_ViewportWidth), float(m_ViewportHeight));
      }
    }
#endif

    auto imageIndex = m_Backend.BeginFrame();
    if (!imageIndex)
    {
      Resize();
      return;
    }

    m_Stats = {};

    auto cmd = m_Backend.GetCurrentCommandBuffer();

    SetUpCamera(app);
    m_FrameUniformBuffer.uniforms.time = (float)app->GetTimer().GetTime();
    m_FrameUniformBuffer.uniforms.gamma = m_Gamma;
    m_FrameUniformBuffer.uniforms.exposure = m_Exposure;
    m_FrameUniformBuffer.uniforms.currentTexture = m_CurrentTexture;
#ifdef YA_EDITOR
    m_FrameUniformBuffer.uniforms.screenWidth = int(m_ViewportWidth);
    m_FrameUniformBuffer.uniforms.screenHeight = int(m_ViewportHeight);
#else
    m_FrameUniformBuffer.uniforms.screenWidth = int(app->GetWindow().GetWidth());
    m_FrameUniformBuffer.uniforms.screenHeight = int(app->GetWindow().GetHeight());
#endif
    m_FrameUniformBuffer.uniforms.ssaoEnabled = b_SSAOEnabled ? 1 : 0;
    m_FrameUniformBuffer.uniforms.ssrEnabled = b_SSREnabled ? 1 : 0;
    m_FrameUniformBuffer.uniforms.taaEnabled = b_TAAEnabled ? 1 : 0;
    m_FrameUniformBuffer.uniforms.hizMipCount = static_cast<int>(m_Graph.GetResourceDesc(m_HiZResource).mipLevels);

    // Configure render graph for this frame (TAA ping-pong + swapchain)
    auto historyWrite = m_TAAIndex == 0 ? m_TAAHistory0 : m_TAAHistory1;
    auto historyRead = m_TAAIndex == 0 ? m_TAAHistory1 : m_TAAHistory0;

    m_Graph.SetPassInput(m_TAAPassIndex, 1, historyRead);
    m_Graph.SetPassColorOutput(m_TAAPassIndex, 0, historyWrite);
    m_Graph.SetPassFramebuffer(m_TAAPassIndex, m_TAAFramebuffers[m_TAAIndex]);

#ifdef YA_EDITOR
    m_Graph.SetPassInput(m_SceneComposePassIndex, 0, historyWrite);

    // SwapchainPass renders ImGui at full window size, override extent
    auto swapExtent = m_Backend.GetSwapChain().GetExt();
    m_Graph.SetPassExtent(m_SwapchainPassIndex, swapExtent);
#else
    m_Graph.SetPassInput(m_SwapchainPassIndex, 0, historyWrite);
#endif
    m_Graph.SetPassFramebuffer(m_SwapchainPassIndex,
      m_Backend.GetSwapChain().GetFramebuffer(*imageIndex));

    // Upload per-frame UBO before executing passes
    auto currentFrame = m_Backend.GetCurrentFrameIndex();
    m_FrameUniformBuffer.SetUp(currentFrame);

    // Execute all passes
    m_Graph.Execute(cmd, app);

    if (!m_Backend.EndFrame(*imageIndex, b_Resized))
    {
      Resize();
    }
    m_TAAIndex = (m_TAAIndex + 1) % 2;
    m_GlobalFrameIndex++;
  }

  VulkanPipeline& Render::GetForwardPipeline(uint8_t key)
  {
    switch (key)
    {
      case 0: return m_ForwardPipeline;
      case 1: return m_ForwardPipelineDoubleSided;
      case 2: return m_ForwardPipelineInstanced;
      case 3: return m_ForwardPipelineDoubleSidedInstanced;
      case 4: return m_ForwardPipelineNoShading;
      default: return m_ForwardPipeline;
    }
  }

  VulkanPipeline& Render::GetDepthPipeline(uint8_t key)
  {
    switch (key)
    {
      case 0: return m_DepthPrepassPipeline;
      case 1: return m_DepthPrepassPipelineDoubleSided;
      case 2: return m_DepthPrepassPipelineInstanced;
      case 3: return m_DepthPrepassPipelineDoubleSidedInstanced;
      default: return m_DepthPrepassPipeline;
    }
  }

  void Render::DrawMeshes(Application* app)
  {
    auto currentFrame = m_Backend.GetCurrentFrameIndex();
    auto cmd = m_Backend.GetCurrentCommandBuffer();
    auto& meshManager = app->GetAssetManager().Meshes();
    auto& materialManager = app->GetAssetManager().Materials();
    auto& cubeMapManager = app->GetAssetManager().CubeMaps();
    auto skybox = app->GetScene().GetSkybox();

    // === Collect ===
    m_DrawCommands.clear();

    auto view = app->GetScene().GetView<MeshComponent, TransformComponent, MaterialComponent>();
    view.each([&](MeshComponent mesh, TransformComponent transform, MaterialComponent material)
    {
      if (!mesh.shouldRender) return;

      auto* instanceData = meshManager.GetInstanceData(mesh.asset);
      bool isInstanced = (instanceData != nullptr) && !mesh.noShading;

      uint8_t pipelineKey;
      if (mesh.noShading)
        pipelineKey = 4;
      else if (isInstanced)
        pipelineKey = mesh.doubleSided ? 3 : 2;
      else
        pipelineKey = mesh.doubleSided ? 1 : 0;

      m_DrawCommands.push_back({
        .pipelineKey = pipelineKey,
        .materialIndex = material.asset.index,
        .materialGeneration = material.asset.generation,
        .meshIndex = mesh.asset.index,
        .meshGeneration = mesh.asset.generation,
        .worldTransform = transform.world,
        .instanceData = instanceData,
        .instanceOffset = meshManager.GetInstanceOffset(mesh.asset),
      });
    });

    // === Sort ===
    std::sort(m_DrawCommands.begin(), m_DrawCommands.end(),
      [](const DrawCommand& a, const DrawCommand& b)
      {
        if (a.pipelineKey != b.pipelineKey) return a.pipelineKey < b.pipelineKey;
        if (a.materialIndex != b.materialIndex) return a.materialIndex < b.materialIndex;
        return a.meshIndex < b.meshIndex;
      });

    // === Update (descriptor sets — host-side, before any cmd recording) ===
    uint32_t lastMaterialIndex = UINT32_MAX;
    uint32_t lastMaterialGeneration = UINT32_MAX;
    for (auto& dc : m_DrawCommands)
    {
      if (dc.materialIndex == lastMaterialIndex && dc.materialGeneration == lastMaterialGeneration) continue;
      lastMaterialIndex = dc.materialIndex;
      lastMaterialGeneration = dc.materialGeneration;

      MaterialHandle matHandle { dc.materialIndex, dc.materialGeneration };
      auto& mat = materialManager.Get(matHandle);
      mat.cubemap = skybox;
      materialManager.GetVulkanMaterial(matHandle).Bind(app, mat, currentFrame, m_NoneTexture);
    }

    // === Record (command buffer) ===
    uint8_t lastPipelineKey = UINT8_MAX;
    lastMaterialIndex = UINT32_MAX;
    uint32_t lastMaterialGen = UINT32_MAX;
    VulkanPipeline* currentPipeline = nullptr;

    for (auto& dc : m_DrawCommands)
    {
      MaterialHandle matHandle { dc.materialIndex, dc.materialGeneration };
      MeshHandle meshHandle { dc.meshIndex, dc.meshGeneration };
      bool isInstanced = dc.instanceData != nullptr;

      if (dc.pipelineKey != lastPipelineKey)
      {
        currentPipeline = &GetForwardPipeline(dc.pipelineKey);
        currentPipeline->Bind(cmd);
        currentPipeline->BindDescriptorSets(cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        lastPipelineKey = dc.pipelineKey;
        lastMaterialIndex = UINT32_MAX;
        lastMaterialGen = UINT32_MAX;
      }

      if (dc.materialIndex != lastMaterialIndex || dc.materialGeneration != lastMaterialGen)
      {
        currentPipeline->BindDescriptorSets(cmd, {materialManager.GetVulkanMaterial(matHandle).GetDescriptorSet(currentFrame)}, 1);
        lastMaterialIndex = dc.materialIndex;
        lastMaterialGen = dc.materialGeneration;
      }

      if (!isInstanced)
      {
        struct
        {
          glm::mat4 model;
          glm::vec4 normalCol0;
          glm::vec4 normalCol1;
          glm::vec4 normalCol2;
          int offset = 0;
        } data;
        data.model = dc.worldTransform;
        glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(dc.worldTransform)));
        data.normalCol0 = glm::vec4(normalMat[0], 0.0f);
        data.normalCol1 = glm::vec4(normalMat[1], 0.0f);
        data.normalCol2 = glm::vec4(normalMat[2], 0.0f);
        currentPipeline->PushConstants(cmd, &data);
      }
      else
      {
        struct
        {
          glm::mat4 model;
          int offset = 0;
        } data;
        data.model = dc.worldTransform;
        data.offset = dc.instanceOffset / sizeof(glm::mat4);
        currentPipeline->PushConstants(cmd, &data);
      }

      uint32_t instanceCount = 1;
      if (isInstanced)
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

    if (!app->GetScene().HasComponent<CameraComponent>(app->GetScene().GetActiveCamera())) return;
    auto& camTransform = app->GetScene().GetComponent<TransformComponent>(app->GetScene().GetActiveCamera());
    glm::mat4 camDir = glm::mat4_cast(camTransform.rotation);
    if (skybox)
      m_SkyBox.Draw(currentFrame, &cubeMapManager.GetVulkanCubicTexture(skybox), cmd, camDir, m_FrameUniformBuffer.uniforms.proj, m_CubicResources);
  }

  void Render::SetUpCamera(Application* app)
  {
    if (!app->GetScene().HasComponent<CameraComponent>(app->GetScene().GetActiveCamera()))
      return;

    auto& transform = app->GetScene().GetComponent<TransformComponent>(app->GetScene().GetActiveCamera());
    auto& camera    = app->GetScene().GetComponent<CameraComponent>(app->GetScene().GetActiveCamera());

    glm::mat4 world =
      glm::translate(glm::mat4(1.0f), transform.position) *
      glm::mat4_cast(transform.rotation);

    glm::mat4 view = glm::inverse(world);

    glm::mat4 proj = glm::perspective(
      camera.fov,
      camera.aspectRatio,
      camera.nearPlane,
      camera.farPlane
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
      jitter.x /= float(app->GetWindow().GetWidth());
      jitter.y /= float(app->GetWindow().GetHeight());
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
    m_FrameUniformBuffer.uniforms.nearPlane = camera.nearPlane;
    m_FrameUniformBuffer.uniforms.farPlane = camera.farPlane;
    m_FrameUniformBuffer.uniforms.cameraPosition = transform.position;
    m_FrameUniformBuffer.uniforms.cameraDirection = glm::normalize(-glm::vec3(world[2]));
    m_FrameUniformBuffer.uniforms.fov = camera.fov;
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

    // Depth prepass pipelines — vertex-only, no fragment shader
    VkRenderPass depthRP = m_Graph.GetPassRenderPass(m_DepthPrepassIndex);

    PipelineCreateInfo depthPrepassInfo = {
      .vertexShaderFile = "depth_prepass.vert",
      .pushConstantSize = sizeof(glm::mat4) + sizeof(int),
      .colorAttachmentCount = 0,
      .vertexInputFormat = "f3f2f3f4",
      .sets = std::vector({
        m_FrameUniformBuffer.GetLayout(),
      })
    };
    m_DepthPrepassPipeline.Init(ctx.device, depthRP, depthPrepassInfo, pipelineCache);
    depthPrepassInfo.doubleSided = true;
    m_DepthPrepassPipelineDoubleSided.Init(ctx.device, depthRP, depthPrepassInfo, pipelineCache);

    depthPrepassInfo.doubleSided = false;
    depthPrepassInfo.vertexShaderFile = "depth_prepass_instanced.vert";
    depthPrepassInfo.sets = std::vector({
      m_FrameUniformBuffer.GetLayout(),
      m_InstanceDescriptorSet.GetLayout(),
    });
    m_DepthPrepassPipelineInstanced.Init(ctx.device, depthRP, depthPrepassInfo, pipelineCache);
    depthPrepassInfo.doubleSided = true;
    m_DepthPrepassPipelineDoubleSidedInstanced.Init(ctx.device, depthRP, depthPrepassInfo, pipelineCache);

    // Forward pipelines — use graph's main pass render pass
    // depthWrite=false, compareOp=EQUAL (depth already written by prepass)
    VkRenderPass mainRP = m_Graph.GetPassRenderPass(m_MainPassIndex);

    PipelineCreateInfo forwardInfo = {
      .fragmentShaderFile = "shader.frag",
      .vertexShaderFile = "shader.vert",
      .pushConstantSize = sizeof(glm::mat4) + 3 * sizeof(glm::vec4) + sizeof(int),
      .depthWrite = false,
      .colorAttachmentCount = 5,
      .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
      .vertexInputFormat = "f3f2f3f4",
      .sets = std::vector({
        m_FrameUniformBuffer.GetLayout(),
        m_DefaultMaterial.GetLayout(),
      })
    };
    m_ForwardPipeline.Init(ctx.device, mainRP, forwardInfo, pipelineCache);
    forwardInfo.doubleSided = true;
    m_ForwardPipelineDoubleSided.Init(ctx.device, mainRP, forwardInfo, pipelineCache);

    forwardInfo.pushConstantSize = sizeof(glm::mat4) + sizeof(int);
    forwardInfo.sets.push_back(m_InstanceDescriptorSet.GetLayout());
    forwardInfo.vertexShaderFile = "instanced.vert",
    m_ForwardPipelineDoubleSidedInstanced.Init(ctx.device, mainRP, forwardInfo, pipelineCache);
    forwardInfo.doubleSided = false;
    m_ForwardPipelineInstanced.Init(ctx.device, mainRP, forwardInfo, pipelineCache);

    forwardInfo.pushConstantSize = sizeof(glm::mat4) + 3 * sizeof(glm::vec4) + sizeof(int);
    forwardInfo.sets.pop_back();
    forwardInfo.doubleSided = true;
    forwardInfo.fragmentShaderFile = "no_shading.frag";
    forwardInfo.vertexShaderFile = "shader.vert";
    m_ForwardPipelineNoShading.Init(ctx.device, mainRP, forwardInfo, pipelineCache);

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

    m_QuadPipeline.Init(ctx.device, quadRP, quadInfo, pipelineCache);

    // TAA pipeline — use TAA render pass (compatible format)
    VkRenderPass taaRP = m_Graph.GetPassRenderPass(m_TAAPassIndex);
    quadInfo.fragmentShaderFile = "taa.frag";
    quadInfo.sets = std::vector({ m_FrameUniformBuffer.GetLayout(), m_TAADescriptorSets[0].GetLayout() });
    m_TAAPipeline.Init(ctx.device, taaRP, quadInfo, pipelineCache);

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
    m_SSAOPipeline.Init(ctx.device, ssaoRP, ssaoInfo, pipelineCache);

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
    m_SSAOBlurHPipeline.Init(ctx.device, ssaoBlurHRP, ssaoBlurInfo, pipelineCache);

    VkRenderPass ssaoBlurVRP = m_Graph.GetPassRenderPass(m_SSAOBlurVPassIndex);
    m_SSAOBlurVPipeline.Init(ctx.device, ssaoBlurVRP, ssaoBlurInfo, pipelineCache);

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
    m_SSRPipeline.Init(ctx.device, ssrRP, ssrPipelineDesc, pipelineCache);

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
    m_HiZPipeline.Init(ctx.device, "hiz.comp",
      { hizLayoutHelper.GetLayout() },
      sizeof(int) * 3,
      pipelineCache);
    hizLayoutHelper.Destroy();
  }

  void Render::DrawMeshesDepthOnly(Application* app)
  {
    auto currentFrame = m_Backend.GetCurrentFrameIndex();
    auto cmd = m_Backend.GetCurrentCommandBuffer();
    auto& meshManager = app->GetAssetManager().Meshes();

    // === Collect ===
    m_DepthDrawCommands.clear();

    auto view = app->GetScene().GetView<MeshComponent, TransformComponent, MaterialComponent>();
    view.each([&](MeshComponent mesh, TransformComponent transform, MaterialComponent material)
    {
      if (!mesh.shouldRender) return;
      if (mesh.noShading) return;

      auto* instanceData = meshManager.GetInstanceData(mesh.asset);
      bool isInstanced = (instanceData != nullptr);

      uint8_t pipelineKey;
      if (isInstanced)
        pipelineKey = mesh.doubleSided ? 3 : 2;
      else
        pipelineKey = mesh.doubleSided ? 1 : 0;

      m_DepthDrawCommands.push_back({
        .pipelineKey = pipelineKey,
        .materialIndex = 0,
        .materialGeneration = 0,
        .meshIndex = mesh.asset.index,
        .meshGeneration = mesh.asset.generation,
        .worldTransform = transform.world,
        .instanceData = instanceData,
        .instanceOffset = meshManager.GetInstanceOffset(mesh.asset),
      });
    });

    // === Sort (by pipeline, then mesh — no material in depth) ===
    std::sort(m_DepthDrawCommands.begin(), m_DepthDrawCommands.end(),
      [](const DrawCommand& a, const DrawCommand& b)
      {
        if (a.pipelineKey != b.pipelineKey) return a.pipelineKey < b.pipelineKey;
        return a.meshIndex < b.meshIndex;
      });

    // === Record ===
    uint8_t lastPipelineKey = UINT8_MAX;
    VulkanPipeline* currentPipeline = nullptr;

    for (auto& dc : m_DepthDrawCommands)
    {
      MeshHandle meshHandle { dc.meshIndex, dc.meshGeneration };
      bool isInstanced = dc.instanceData != nullptr;

      if (dc.pipelineKey != lastPipelineKey)
      {
        currentPipeline = &GetDepthPipeline(dc.pipelineKey);
        currentPipeline->Bind(cmd);
        currentPipeline->BindDescriptorSets(cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        lastPipelineKey = dc.pipelineKey;
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
      if (isInstanced)
      {
        instanceCount = uint32_t(dc.instanceData->size());
        currentPipeline->BindDescriptorSets(cmd, { m_InstanceDescriptorSet.Get() }, 1);
        m_InstanceBuffer.Update(dc.instanceOffset, dc.instanceData->data(), uint32_t(instanceCount * sizeof(glm::mat4)));
      }

      auto& vb = meshManager.GetVertexBuffer(meshHandle);
      vb.Draw(cmd, instanceCount);
    }
  }

  void Render::CreateHiZResources()
  {
    auto& ctx = m_Backend.GetContext();
    auto hizImage = m_Graph.GetResourceImage(m_HiZResource);
    auto& hizDesc = m_Graph.GetResourceDesc(m_HiZResource);
    uint32_t mipCount = hizDesc.mipLevels;

    // Create per-mip image views for storage writes
    m_HiZMipViews.resize(mipCount);
    for (uint32_t mip = 0; mip < mipCount; mip++)
    {
      VkImageViewCreateInfo viewInfo{};
      viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      viewInfo.image = hizImage;
      viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format = VK_FORMAT_R32_SFLOAT;
      viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      viewInfo.subresourceRange.baseMipLevel = mip;
      viewInfo.subresourceRange.levelCount = 1;
      viewInfo.subresourceRange.baseArrayLayer = 0;
      viewInfo.subresourceRange.layerCount = 1;

      if (vkCreateImageView(ctx.device, &viewInfo, nullptr, &m_HiZMipViews[mip]) != VK_SUCCESS)
      {
        YA_LOG_ERROR("Render", "Failed to create Hi-Z mip view %d", mip);
        throw std::runtime_error("Failed to create Hi-Z mip view!");
      }
    }

    // Create per-mip descriptor sets for Hi-Z compute
    SetDescription hizSetDesc = {
      .set = 0,
      .bindings = {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT }
      }
    };

    m_HiZDescriptorSets.resize(mipCount);
    for (uint32_t mip = 0; mip < mipCount; mip++)
    {
      if (mip == 0)
        m_HiZDescriptorSets[mip].Init(ctx, hizSetDesc);
      else
        m_HiZDescriptorSets[mip].Init(ctx, m_HiZDescriptorSets[0].GetLayout());
    }

    // Write descriptors
    auto& depth = m_Graph.GetResource(m_MainDepth);
    auto& hiZ = m_Graph.GetResource(m_HiZResource);

    for (uint32_t mip = 0; mip < mipCount; mip++)
    {
      if (mip == 0)
      {
        // Mip 0: read from depth buffer
        m_HiZDescriptorSets[mip].WriteCombinedImageSampler(0,
          depth.GetView(), depth.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      }
      else
      {
        // Mip N: read from full Hi-Z texture (in GENERAL layout during compute)
        m_HiZDescriptorSets[mip].WriteCombinedImageSampler(0,
          hiZ.GetView(), hiZ.GetSampler(), VK_IMAGE_LAYOUT_GENERAL);
      }

      // Storage image write for this mip level
      m_HiZDescriptorSets[mip].WriteStorageImage(1, m_HiZMipViews[mip], VK_IMAGE_LAYOUT_GENERAL);
    }
  }

  void Render::DestroyHiZResources()
  {
    auto& ctx = m_Backend.GetContext();

    for (auto& set : m_HiZDescriptorSets)
      set.Destroy();
    m_HiZDescriptorSets.clear();

    for (auto view : m_HiZMipViews)
    {
      if (view != VK_NULL_HANDLE)
        vkDestroyImageView(ctx.device, view, nullptr);
    }
    m_HiZMipViews.clear();
  }

  void Render::DrawQuad()
  {
    vkCmdDraw(m_Backend.GetCurrentCommandBuffer(), 3, 1, 0, 0);
  }
}
