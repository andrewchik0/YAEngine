#include "VulkanFramebuffer.h"

#include "RenderContext.h"
#include "ImageBarrier.h"

namespace YAEngine
{
  void VulkanFramebuffer::Init(const RenderContext& ctx, VkRenderPass renderPass, uint32_t width, uint32_t height, VkFormat format, bool secondaryImageBuffer)
  {
    m_RenderPass = renderPass;
    m_Format = format;
    m_Device = ctx.device;
    b_HasSecondary = secondaryImageBuffer;

    ImageDesc depthDesc;
    depthDesc.width = width;
    depthDesc.height = height;
    depthDesc.format = VK_FORMAT_D32_SFLOAT;
    depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    depthDesc.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

    SamplerDesc depthSampler;
    depthSampler.magFilter = VK_FILTER_NEAREST;
    depthSampler.minFilter = VK_FILTER_NEAREST;

    m_DepthAttachment.Init(ctx, depthDesc, &depthSampler);

    ImageDesc colorDesc;
    colorDesc.width = width;
    colorDesc.height = height;
    colorDesc.format = format;
    colorDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    colorDesc.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    SamplerDesc colorSampler;

    m_ColorAttachment.Init(ctx, colorDesc, &colorSampler);

    if (secondaryImageBuffer)
    {
      m_SecondaryAttachment.Init(ctx, colorDesc, &colorSampler);
    }

    uint32_t attachmentCount = 2 + (secondaryImageBuffer ? 1 : 0);
    VkImageView attachments[3] = {
      m_ColorAttachment.GetView(),
      m_DepthAttachment.GetView(),
    };
    if (secondaryImageBuffer)
    {
      attachments[2] = m_SecondaryAttachment.GetView();
    }

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_RenderPass;
    framebufferInfo.attachmentCount = attachmentCount;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = width;
    framebufferInfo.height = height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(ctx.device, &framebufferInfo, nullptr, &m_Framebuffer) != VK_SUCCESS)
    {
      throw std::runtime_error("Failed to create framebuffer!");
    }
  }

  void VulkanFramebuffer::Destroy(const RenderContext& ctx)
  {
    vkDeviceWaitIdle(ctx.device);

    m_DepthAttachment.Destroy(ctx);
    m_ColorAttachment.Destroy(ctx);
    m_SecondaryAttachment.Destroy(ctx);

    vkDestroyFramebuffer(ctx.device, m_Framebuffer, nullptr);
    m_Framebuffer = VK_NULL_HANDLE;
  }

  void VulkanFramebuffer::Begin(VkCommandBuffer cmd)
  {
    TransitionImageLayout(cmd, m_ColorAttachment.GetImage(),
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    m_ColorAttachment.SetLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    TransitionImageLayout(cmd, m_DepthAttachment.GetImage(),
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      VK_IMAGE_ASPECT_DEPTH_BIT);
    m_DepthAttachment.SetLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    if (m_SecondaryAttachment.IsValid())
    {
      TransitionImageLayout(cmd, m_SecondaryAttachment.GetImage(),
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
      m_SecondaryAttachment.SetLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
  }

  void VulkanFramebuffer::End(VkCommandBuffer cmd)
  {
    TransitionImageLayout(cmd, m_ColorAttachment.GetImage(),
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_ColorAttachment.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    TransitionImageLayout(cmd, m_DepthAttachment.GetImage(),
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_DEPTH_BIT);
    m_DepthAttachment.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    if (m_SecondaryAttachment.IsValid())
    {
      TransitionImageLayout(cmd, m_SecondaryAttachment.GetImage(),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      m_SecondaryAttachment.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
  }

  void VulkanFramebuffer::Recreate(const RenderContext& ctx, VkRenderPass renderPass, uint32_t width, uint32_t height)
  {
    m_RenderPass = renderPass;
    vkDeviceWaitIdle(ctx.device);
    Destroy(ctx);
    Init(ctx, m_RenderPass, width, height, m_Format, b_HasSecondary);
  }
}
