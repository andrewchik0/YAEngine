#pragma once

#include "Pch.h"
#include "VulkanImage.h"

namespace YAEngine
{
  struct RenderContext;

  class VulkanFramebuffer
  {
  public:

    void Init(const RenderContext& ctx, VkRenderPass renderPass, uint32_t width, uint32_t height, VkFormat format, bool secondaryImageBuffer = false);
    void Destroy(const RenderContext& ctx);

    void Begin(VkCommandBuffer cmd);
    void End(VkCommandBuffer cmd);

    void Recreate(const RenderContext& ctx, VkRenderPass renderPass, uint32_t width, uint32_t height);

    VkFramebuffer& Get() { return m_Framebuffer; }

    VkImageView GetImageView() { return m_ColorAttachment.GetView(); }
    VkImageLayout GetLayout() { return m_ColorAttachment.GetLayout(); }
    VkSampler GetSampler() { return m_ColorAttachment.GetSampler(); }
    VkImage GetImage() { return m_ColorAttachment.GetImage(); }

    VkImageView GetDepthImageView() { return m_DepthAttachment.GetView(); }
    VkImageLayout GetDepthLayout() { return m_DepthAttachment.GetLayout(); }
    VkSampler GetDepthSampler() { return m_DepthAttachment.GetSampler(); }
    VkImage GetDepthImage() { return m_DepthAttachment.GetImage(); }

    VkImageView GetSecondaryImageView() { return m_SecondaryAttachment.GetView(); }
    VkImageLayout GetSecondaryLayout() { return m_SecondaryAttachment.GetLayout(); }
    VkSampler GetSecondarySampler() { return m_SecondaryAttachment.GetSampler(); }
    VkImage GetSecondaryImage() { return m_SecondaryAttachment.GetImage(); }

  private:

    bool b_HasSecondary = false;

    VkFramebuffer m_Framebuffer {};

    VulkanImage m_ColorAttachment;
    VulkanImage m_DepthAttachment;
    VulkanImage m_SecondaryAttachment;

    VkRenderPass m_RenderPass {};
    VkFormat m_Format {};
    VkDevice m_Device {};
  };
}
