#pragma once

#include "Pch.h"

namespace YAEngine
{
  struct RenderContext;

  struct ImageDesc
  {
    uint32_t width = 1;
    uint32_t height = 1;
    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    VkImageCreateFlags flags = 0;
    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  };

  struct SamplerDesc
  {
    VkFilter magFilter = VK_FILTER_LINEAR;
    VkFilter minFilter = VK_FILTER_LINEAR;
    VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    bool anisotropyEnable = false;
    float maxAnisotropy = 1.0f;
    float minLod = 0.0f;
    float maxLod = 0.0f;
  };

  class VulkanImage
  {
  public:

    void Init(const RenderContext& ctx, const ImageDesc& desc, const SamplerDesc* samplerDesc = nullptr);
    void Destroy(const RenderContext& ctx);

    VkImage GetImage() const { return m_Image; }
    VkImageView GetView() const { return m_View; }
    VkSampler GetSampler() const { return m_Sampler; }
    VkImageLayout GetLayout() const { return m_Layout; }
    VkFormat GetFormat() const { return m_Format; }

    void SetLayout(VkImageLayout layout) { m_Layout = layout; }

    bool IsValid() const { return m_Image != VK_NULL_HANDLE; }

  private:

    VkImage m_Image {};
    VmaAllocation m_Allocation {};
    VkImageView m_View {};
    VkSampler m_Sampler {};
    VkImageLayout m_Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkFormat m_Format {};
    VkImageAspectFlags m_AspectMask {};
  };
}
