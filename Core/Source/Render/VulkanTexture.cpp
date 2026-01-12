#include "VulkanTexture.h"

#include "VulkanCommandBuffer.h"

namespace YAEngine
{
  VkQueue VulkanTexture::s_Queue = VK_NULL_HANDLE;
  VmaAllocator VulkanTexture::s_Allocator = VK_NULL_HANDLE;
  VulkanCommandBuffer* VulkanTexture::s_CommandBuffer = VK_NULL_HANDLE;
  VkDevice VulkanTexture::s_Device = VK_NULL_HANDLE;

  void VulkanTexture::InitTextures(VkDevice device, VkQueue queue, VulkanCommandBuffer& commandBuffer, VmaAllocator allocator)
  {
    s_Device = device;
    s_Queue = queue;
    s_Allocator = allocator;
    s_CommandBuffer = &commandBuffer;
  }

  void VulkanTexture::Load(void* inputData, uint32_t width, uint32_t height, uint32_t pixelSize, VkFormat imageFormat, bool repeat)
  {
    VkDeviceSize imageSize = width * height * pixelSize;

    VkBufferCreateInfo stagingBufferInfo{};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = imageSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocation stagingAlloc;
    VkBuffer stagingBuffer;
    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    if (vmaCreateBuffer(s_Allocator, &stagingBufferInfo, &stagingAllocInfo, &stagingBuffer, &stagingAlloc, nullptr) != VK_SUCCESS)
    {
      throw std::exception("Failed to create vertex staging buffer");
    }

    void* data;
    vmaMapMemory(s_Allocator, stagingAlloc, &data);
    memcpy(data, inputData, (size_t)imageSize);
    vmaUnmapMemory(s_Allocator, stagingAlloc);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width  = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth  = 1;
    imageInfo.mipLevels     = uint32_t(width > height ? glm::log2((float)width) : glm::log2((float)height)) || 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.format        = imageFormat;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    int32_t result;
    if ((result = vmaCreateImage(s_Allocator, &imageInfo, &allocInfo, &m_TextureImage, &m_TextureImageAllocation, nullptr)) != VK_SUCCESS)
    {
      throw std::runtime_error("Failed to create VMA texture image!");
    }

    auto TransitionImageLayout = [&](VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
      VkCommandBuffer cmd = s_CommandBuffer->BeginSingleTimeCommands();

      VkImageMemoryBarrier barrier{};
      barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      barrier.oldLayout = oldLayout;
      barrier.newLayout = newLayout;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image = image;
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.baseMipLevel = 0;
      barrier.subresourceRange.levelCount = 1;
      barrier.subresourceRange.baseArrayLayer = 0;
      barrier.subresourceRange.layerCount = 1;

      VkPipelineStageFlags sourceStage;
      VkPipelineStageFlags destinationStage;

      if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      } else {
        throw std::invalid_argument("Unsupported layout transition!");
      }

      vkCmdPipelineBarrier(
          cmd,
          sourceStage, destinationStage,
          0,
          0, nullptr,
          0, nullptr,
          1, &barrier
      );

      s_CommandBuffer->EndSingleTimeCommands(cmd);
    };

    TransitionImageLayout(m_TextureImage, imageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkCommandBuffer cmd = s_CommandBuffer->BeginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = {0, 0, 0};
    region.imageExtent = { width, height, 1 };

    vkCmdCopyBufferToImage(
      cmd,
      stagingBuffer,
      m_TextureImage,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      1,
      &region
    );

    s_CommandBuffer->EndSingleTimeCommands(cmd);

    TransitionImageLayout(m_TextureImage, imageFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vmaDestroyBuffer(s_Allocator, stagingBuffer, stagingAlloc);

    // ImageView
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_TextureImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageFormat;

    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(s_Device, &viewInfo, nullptr, &m_TextureImageView) != VK_SUCCESS)
    {
      throw std::runtime_error("Failed to create texture image view!");
    }

    // Image Sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;

    samplerInfo.addressModeU = repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;

    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(s_Device, &samplerInfo, nullptr, &m_TextureSampler) != VK_SUCCESS)
    {
      throw std::runtime_error("Failed to create texture sampler!");
    }
  }

  void VulkanTexture::Destroy()
  {
    if (m_TextureImage != VK_NULL_HANDLE || m_TextureImageAllocation != VK_NULL_HANDLE)
    {
      vmaDestroyImage(s_Allocator, m_TextureImage, m_TextureImageAllocation);
      m_TextureImageAllocation = VK_NULL_HANDLE;
      m_TextureImage = VK_NULL_HANDLE;
    }
    if (m_TextureSampler != VK_NULL_HANDLE)
    {
      vkDestroySampler(s_Device, m_TextureSampler, nullptr);
      m_TextureSampler = VK_NULL_HANDLE;
    }
    if (m_TextureImageView != VK_NULL_HANDLE)
    {
      vkDestroyImageView(s_Device, m_TextureImageView, nullptr);
      m_TextureImageView = VK_NULL_HANDLE;
    }
  }
}
