#include "VulkanImage.h"

#include "RenderContext.h"
#include "Utils/Log.h"

namespace YAEngine
{
  void VulkanImage::Init(const RenderContext& ctx, const ImageDesc& desc, const SamplerDesc* samplerDesc)
  {
    m_Format = desc.format;
    m_AspectMask = desc.aspectMask;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = desc.flags;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = desc.width;
    imageInfo.extent.height = desc.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = desc.arrayLayers;
    imageInfo.format = desc.format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = desc.usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(ctx.allocator, &imageInfo, &allocInfo, &m_Image, &m_Allocation, nullptr) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create image");
      throw std::runtime_error("Failed to create image!");
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_Image;
    viewInfo.viewType = desc.viewType;
    viewInfo.format = desc.format;
    viewInfo.subresourceRange.aspectMask = desc.aspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = desc.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = desc.arrayLayers;

    if (vkCreateImageView(ctx.device, &viewInfo, nullptr, &m_View) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create image view");
      throw std::runtime_error("Failed to create image view!");
    }

    if (samplerDesc)
    {
      VkSamplerCreateInfo samplerInfo{};
      samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
      samplerInfo.magFilter = samplerDesc->magFilter;
      samplerInfo.minFilter = samplerDesc->minFilter;
      samplerInfo.addressModeU = samplerDesc->addressMode;
      samplerInfo.addressModeV = samplerDesc->addressMode;
      samplerInfo.addressModeW = samplerDesc->addressMode;
      samplerInfo.anisotropyEnable = samplerDesc->anisotropyEnable ? VK_TRUE : VK_FALSE;
      samplerInfo.maxAnisotropy = samplerDesc->maxAnisotropy;
      samplerInfo.borderColor = samplerDesc->borderColor;
      samplerInfo.unnormalizedCoordinates = VK_FALSE;
      samplerInfo.compareEnable = VK_FALSE;
      samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
      samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      samplerInfo.mipLodBias = 0.0f;
      samplerInfo.minLod = samplerDesc->minLod;
      samplerInfo.maxLod = samplerDesc->maxLod;

      if (vkCreateSampler(ctx.device, &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS)
      {
        YA_LOG_ERROR("Render", "Failed to create sampler");
        throw std::runtime_error("Failed to create sampler!");
      }
    }

    m_Layout = VK_IMAGE_LAYOUT_UNDEFINED;
  }

  void VulkanImage::Destroy(const RenderContext& ctx)
  {
    if (m_Sampler != VK_NULL_HANDLE)
    {
      vkDestroySampler(ctx.device, m_Sampler, nullptr);
      m_Sampler = VK_NULL_HANDLE;
    }
    if (m_View != VK_NULL_HANDLE)
    {
      vkDestroyImageView(ctx.device, m_View, nullptr);
      m_View = VK_NULL_HANDLE;
    }
    if (m_Image != VK_NULL_HANDLE)
    {
      vmaDestroyImage(ctx.allocator, m_Image, m_Allocation);
      m_Image = VK_NULL_HANDLE;
      m_Allocation = VK_NULL_HANDLE;
    }
  }
}
