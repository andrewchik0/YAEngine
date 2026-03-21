#pragma once

#include "Pch.h"
#include "VulkanImage.h"

namespace YAEngine
{
  struct RenderContext;

  class VulkanTexture
  {
  public:

    void Load(const RenderContext& ctx, void* data, uint32_t width, uint32_t height, uint32_t pixelSize, VkFormat format, bool repeat = true);
    void Destroy(const RenderContext& ctx);

    VkImageView GetView() const { return m_Image.GetView(); }
    VkSampler GetSampler() const { return m_Image.GetSampler(); }
    VkImage GetImage() const { return m_Image.GetImage(); }

    bool IsValid() const { return m_Image.IsValid(); }

  private:

    VulkanImage m_Image;
  };
}
