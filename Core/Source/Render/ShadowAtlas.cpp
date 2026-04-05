#include "ShadowAtlas.h"
#include "RenderContext.h"
#include "VulkanCommandBuffer.h"
#include "Utils/Log.h"

namespace YAEngine
{
  void ShadowAtlas::Init(const RenderContext& ctx)
  {
    // Create depth image
    ImageDesc imageDesc;
    imageDesc.width = SHADOW_ATLAS_SIZE;
    imageDesc.height = SHADOW_ATLAS_SIZE;
    imageDesc.format = VK_FORMAT_D32_SFLOAT;
    imageDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageDesc.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

    SamplerDesc samplerDesc;
    samplerDesc.magFilter = VK_FILTER_NEAREST;
    samplerDesc.minFilter = VK_FILTER_NEAREST;
    samplerDesc.addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerDesc.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

    m_Image.Init(ctx, imageDesc, &samplerDesc);

    // Create render pass (depth-only, clear on load)
    VkAttachmentDescription depthAttachment {};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthRef {};
    depthRef.attachment = 0;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency {};
    dependency.srcSubpass = 0;
    dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpInfo {};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &depthAttachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(ctx.device, &rpInfo, nullptr, &m_RenderPass) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create shadow atlas render pass");
      throw std::runtime_error("Failed to create shadow atlas render pass!");
    }

    // Create framebuffer
    VkImageView view = m_Image.GetView();

    VkFramebufferCreateInfo fbInfo {};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = m_RenderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &view;
    fbInfo.width = SHADOW_ATLAS_SIZE;
    fbInfo.height = SHADOW_ATLAS_SIZE;
    fbInfo.layers = 1;

    if (vkCreateFramebuffer(ctx.device, &fbInfo, nullptr, &m_Framebuffer) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create shadow atlas framebuffer");
      throw std::runtime_error("Failed to create shadow atlas framebuffer!");
    }

    // Run an empty render pass to transition image to DEPTH_STENCIL_READ_ONLY_OPTIMAL.
    // Without this, the deferred lighting pass would sample from an UNDEFINED layout
    // on frames where shadow rendering is skipped.
    {
      auto cmd = ctx.commandBuffer->BeginSingleTimeCommands();

      VkClearValue clearValue {};
      clearValue.depthStencil = { 1.0f, 0 };

      VkRenderPassBeginInfo rpBegin {};
      rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      rpBegin.renderPass = m_RenderPass;
      rpBegin.framebuffer = m_Framebuffer;
      rpBegin.renderArea = { {0, 0}, { SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE } };
      rpBegin.clearValueCount = 1;
      rpBegin.pClearValues = &clearValue;

      vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdEndRenderPass(cmd);

      ctx.commandBuffer->EndSingleTimeCommands(cmd);

      m_Image.SetLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    }

    YA_LOG_INFO("Render", "Shadow atlas created (%dx%d, D32_SFLOAT)", SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE);
  }

  void ShadowAtlas::Destroy(const RenderContext& ctx)
  {
    if (m_Framebuffer)
    {
      vkDestroyFramebuffer(ctx.device, m_Framebuffer, nullptr);
      m_Framebuffer = VK_NULL_HANDLE;
    }
    if (m_RenderPass)
    {
      vkDestroyRenderPass(ctx.device, m_RenderPass, nullptr);
      m_RenderPass = VK_NULL_HANDLE;
    }
    m_Image.Destroy(ctx);
  }
}
