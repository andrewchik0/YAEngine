#include "VulkanFramebuffer.h"

#include <format>

namespace YAEngine
{
  void VulkanFramebuffer::Init(VkDevice device, VmaAllocator allocator, VkRenderPass renderPass, uint32_t width, uint32_t height, VkFormat format, bool secondaryImageBuffer)
  {
    m_RenderPass = renderPass;
    m_Allocator = allocator;
    m_Device = device;
    m_Format = format;
    b_SecondaryBuffer = secondaryImageBuffer;

    VkImageCreateInfo depthImageInfo{};
    depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImageInfo.extent.width = width;
    depthImageInfo.extent.height = height;
    depthImageInfo.extent.depth = 1;
    depthImageInfo.mipLevels = 1;
    depthImageInfo.arrayLayers = 1;
    depthImageInfo.format = VK_FORMAT_D32_SFLOAT;
    depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo depthAllocInfo{};
    depthAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(m_Allocator, &depthImageInfo, &depthAllocInfo, &m_DepthImage, &m_DepthImageAllocation, nullptr) != VK_SUCCESS)
      throw std::runtime_error("Failed to create depth image!");

    VkImageViewCreateInfo depthViewInfo{};
    depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthViewInfo.image = m_DepthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = VK_FORMAT_D32_SFLOAT;
    depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthViewInfo.subresourceRange.baseMipLevel = 0;
    depthViewInfo.subresourceRange.levelCount = 1;
    depthViewInfo.subresourceRange.baseArrayLayer = 0;
    depthViewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Device, &depthViewInfo, nullptr, &m_DepthImageView) != VK_SUCCESS)
    {
      throw std::runtime_error("Failed to create depth image view!");
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width  = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth  = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = m_Format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(m_Allocator, &imageInfo, &allocInfo, &m_Image, &m_ImageAllocation, nullptr) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create framebuffer image!");
    }

    if (secondaryImageBuffer)
    {
      VkImageCreateInfo secondaryImageView{};
      secondaryImageView.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
      secondaryImageView.imageType = VK_IMAGE_TYPE_2D;
      secondaryImageView.extent.width  = width;
      secondaryImageView.extent.height = height;
      secondaryImageView.extent.depth  = 1;
      secondaryImageView.mipLevels = 1;
      secondaryImageView.arrayLayers = 1;
      secondaryImageView.format = m_Format;
      secondaryImageView.tiling = VK_IMAGE_TILING_OPTIMAL;
      secondaryImageView.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      secondaryImageView.usage =
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
          VK_IMAGE_USAGE_SAMPLED_BIT;
      secondaryImageView.samples = VK_SAMPLE_COUNT_1_BIT;
      secondaryImageView.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

      VmaAllocationCreateInfo secondaryAllocInfo {};
      secondaryAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

      if (vmaCreateImage(m_Allocator, &secondaryImageView, &secondaryAllocInfo, &m_SecondaryImage, &m_SecondaryImageAllocation, nullptr) != VK_SUCCESS)
      {
        throw std::runtime_error("failed to create framebuffer image!");
      }
    }

    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = m_Image;
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = m_Format;
    createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &createInfo, nullptr, &m_ImageView) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create image views!");
    }

    if (secondaryImageBuffer)
    {
      VkImageViewCreateInfo secondaryInfo{};
      secondaryInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      secondaryInfo.image = m_SecondaryImage;
      secondaryInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      secondaryInfo.format = m_Format;
      secondaryInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
      secondaryInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
      secondaryInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
      secondaryInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
      secondaryInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      secondaryInfo.subresourceRange.baseMipLevel = 0;
      secondaryInfo.subresourceRange.levelCount = 1;
      secondaryInfo.subresourceRange.baseArrayLayer = 0;
      secondaryInfo.subresourceRange.layerCount = 1;

      if (vkCreateImageView(device, &secondaryInfo, nullptr, &m_SecondaryImageView) != VK_SUCCESS)
      {
        throw std::runtime_error("failed to create image views!");
      }
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    vkCreateSampler(device, &samplerInfo, nullptr, &m_Sampler);

    VkSamplerCreateInfo depthSampler{};
    depthSampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    depthSampler.magFilter = VK_FILTER_NEAREST;
    depthSampler.minFilter = VK_FILTER_NEAREST;
    depthSampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    depthSampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    depthSampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    vkCreateSampler(device, &depthSampler, nullptr, &m_DepthSampler);

    if (secondaryImageBuffer)
    {
      VkSamplerCreateInfo secondarySampler{};
      secondarySampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
      secondarySampler.magFilter = VK_FILTER_LINEAR;
      secondarySampler.minFilter = VK_FILTER_LINEAR;
      secondarySampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      secondarySampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      secondarySampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

      vkCreateSampler(device, &secondarySampler, nullptr, &m_SecondarySampler);
    }


    VkImageView attachments[3] = {
      m_ImageView,
      m_DepthImageView,
    };

    if (secondaryImageBuffer)
    {
      attachments[2] = m_SecondaryImageView;
    }

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_RenderPass;
    framebufferInfo.attachmentCount = 2 + secondaryImageBuffer;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = width;
    framebufferInfo.height = height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(m_Device, &framebufferInfo, nullptr, &m_Framebuffer) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create framebuffer!");
    }

    m_ImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  }

  void VulkanFramebuffer::Destroy()
  {
    vkDeviceWaitIdle(m_Device);

    vkDestroyImageView(m_Device, m_DepthImageView, nullptr);
    vmaDestroyImage(m_Allocator, m_DepthImage, m_DepthImageAllocation);
    vkDestroySampler(m_Device, m_DepthSampler, nullptr);

    vkDestroyImageView(m_Device, m_ImageView, nullptr);
    vmaDestroyImage(m_Allocator, m_Image, m_ImageAllocation);
    vkDestroySampler(m_Device, m_Sampler, nullptr);

    vkDestroyImageView(m_Device, m_SecondaryImageView, nullptr);
    vmaDestroyImage(m_Allocator, m_SecondaryImage, m_SecondaryImageAllocation);
    vkDestroySampler(m_Device, m_SecondarySampler, nullptr);

    vkDestroyFramebuffer(m_Device, m_Framebuffer, nullptr);
  }

  void VulkanFramebuffer::Begin(VkCommandBuffer cmd)
  {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.image = m_Image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
      cmd,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      0, 0, nullptr, 0, nullptr, 1, &barrier
    );

    VkImageMemoryBarrier depthBarrier {};
    depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthBarrier.srcAccessMask = 0;
    depthBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthBarrier.image = m_DepthImage;
    depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthBarrier.subresourceRange.baseMipLevel = 0;
    depthBarrier.subresourceRange.levelCount = 1;
    depthBarrier.subresourceRange.baseArrayLayer = 0;
    depthBarrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
      cmd,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
      0, 0, nullptr, 0, nullptr, 1, &depthBarrier
    );

    if (m_SecondaryImage != VK_NULL_HANDLE)
    {
      VkImageMemoryBarrier secondaryBarrier{};
      secondaryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      secondaryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      secondaryBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      secondaryBarrier.srcAccessMask = 0;
      secondaryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      secondaryBarrier.image = m_SecondaryImage;
      secondaryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      secondaryBarrier.subresourceRange.baseMipLevel = 0;
      secondaryBarrier.subresourceRange.levelCount = 1;
      secondaryBarrier.subresourceRange.baseArrayLayer = 0;
      secondaryBarrier.subresourceRange.layerCount = 1;

      vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, nullptr, 0, nullptr, 1, &secondaryBarrier
      );
    }

    m_ImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    m_DepthImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    m_SecondaryImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  }

  void VulkanFramebuffer::End(VkCommandBuffer cmd)
  {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.image = m_Image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier
    );

    VkImageMemoryBarrier depthBarrier{};
    depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    depthBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depthBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    depthBarrier.image = m_DepthImage;
    depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthBarrier.subresourceRange.baseMipLevel = 0;
    depthBarrier.subresourceRange.levelCount = 1;
    depthBarrier.subresourceRange.baseArrayLayer = 0;
    depthBarrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &depthBarrier
    );

    if (m_SecondaryImage != VK_NULL_HANDLE)
    {
      VkImageMemoryBarrier secondaryBarrier{};
      secondaryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      secondaryBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      secondaryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      secondaryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      secondaryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      secondaryBarrier.image = m_SecondaryImage;
      secondaryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      secondaryBarrier.subresourceRange.baseMipLevel = 0;
      secondaryBarrier.subresourceRange.levelCount = 1;
      secondaryBarrier.subresourceRange.baseArrayLayer = 0;
      secondaryBarrier.subresourceRange.layerCount = 1;

      vkCmdPipelineBarrier(
          cmd,
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          0, 0, nullptr, 0, nullptr, 1, &secondaryBarrier
      );
    }

    m_ImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    m_DepthImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    m_SecondaryImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }

  void VulkanFramebuffer::Recreate(VkRenderPass renderPass, uint32_t width, uint32_t height)
  {
    m_RenderPass = renderPass;
    vkDeviceWaitIdle(m_Device);

    Destroy();
    Init(m_Device, m_Allocator, m_RenderPass, width, height, m_Format, b_SecondaryBuffer);
  }
}
