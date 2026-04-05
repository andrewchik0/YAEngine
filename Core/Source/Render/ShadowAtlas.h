#pragma once

#include "VulkanImage.h"
#include "ShadowData.h"

namespace YAEngine
{
  struct RenderContext;

  class ShadowAtlas
  {
  public:
    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    VkRenderPass GetRenderPass() const { return m_RenderPass; }
    VkFramebuffer GetFramebuffer() const { return m_Framebuffer; }
    VkExtent2D GetExtent() const { return { SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE }; }

    VkImageView GetView() const { return m_Image.GetView(); }
    VkSampler GetSampler() const { return m_Image.GetSampler(); }
    VkImage GetImage() const { return m_Image.GetImage(); }

  private:
    VulkanImage m_Image;
    VkRenderPass m_RenderPass = VK_NULL_HANDLE;
    VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
  };
}
