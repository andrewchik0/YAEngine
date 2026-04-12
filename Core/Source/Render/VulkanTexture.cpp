#include "VulkanTexture.h"

#include "RenderContext.h"
#include "VulkanBuffer.h"
#include "VulkanCommandBuffer.h"
#include "ImageBarrier.h"
#include "Assets/CpuResourceData.h"
#include "Utils/MipGenerator.h"

namespace YAEngine
{

  void VulkanTexture::Load(const RenderContext& ctx, void* inputData, uint32_t width, uint32_t height, uint32_t pixelSize, VkFormat imageFormat, bool repeat, bool preserveAlphaCoverage)
  {
    uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

    ImageDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = imageFormat;
    desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.mipLevels = mipLevels;
    desc.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    SamplerDesc sampler;
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.addressMode = repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.anisotropyEnable = true;
    sampler.maxAnisotropy = 16.0f;
    sampler.maxLod = static_cast<float>(mipLevels);

    m_Image.Init(ctx, desc, &sampler);

    if (preserveAlphaCoverage && pixelSize == 4)
    {
      std::vector<CpuMipLevel> mips;
      MipGenerator::GenerateWithAlphaCoverage(static_cast<uint8_t*>(inputData), width, height, mipLevels, mips);

      // Compute total staging size
      VkDeviceSize totalSize = 0;
      for (auto& mip : mips)
        totalSize += static_cast<VkDeviceSize>(mip.width) * mip.height * 4;

      VulkanBuffer staging = VulkanBuffer::CreateMapped(ctx, totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
      VkDeviceSize offset = 0;
      for (auto& mip : mips)
      {
        uint32_t mipSize = mip.width * mip.height * 4;
        staging.Update(static_cast<uint32_t>(offset), mip.data.data(), mipSize);
        offset += mipSize;
      }

      VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();

      TransitionImageLayout(cmd, m_Image.GetImage(),
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels);

      offset = 0;
      for (uint32_t m = 0; m < mipLevels; m++)
      {
        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = m;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { mips[m].width, mips[m].height, 1 };

        vkCmdCopyBufferToImage(cmd, staging.Get(), m_Image.GetImage(),
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        offset += static_cast<VkDeviceSize>(mips[m].width) * mips[m].height * 4;
      }

      TransitionImageLayout(cmd, m_Image.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels);

      ctx.commandBuffer->EndSingleTimeCommands(cmd);
      staging.Destroy(ctx);
    }
    else
    {
      // GPU blit mip chain (existing path)
      VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * pixelSize;
      VulkanBuffer staging = VulkanBuffer::CreateMapped(ctx, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
      staging.Update(0, inputData, static_cast<uint32_t>(imageSize));

      VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();

      TransitionImageLayout(cmd, m_Image.GetImage(),
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels);

      VkBufferImageCopy region{};
      region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.imageSubresource.mipLevel = 0;
      region.imageSubresource.baseArrayLayer = 0;
      region.imageSubresource.layerCount = 1;
      region.imageExtent = { width, height, 1 };

      vkCmdCopyBufferToImage(cmd, staging.Get(), m_Image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

      int32_t mipWidth = static_cast<int32_t>(width);
      int32_t mipHeight = static_cast<int32_t>(height);

      for (uint32_t i = 1; i < mipLevels; i++)
      {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = m_Image.GetImage();
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
          0,
          0, nullptr,
          0, nullptr,
          1, &barrier);

        int32_t nextWidth = mipWidth > 1 ? mipWidth / 2 : 1;
        int32_t nextHeight = mipHeight > 1 ? mipHeight / 2 : 1;

        VkImageBlit blit{};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { nextWidth, nextHeight, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(cmd,
          m_Image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          m_Image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          1, &blit,
          VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          0,
          0, nullptr,
          0, nullptr,
          1, &barrier);

        mipWidth = nextWidth;
        mipHeight = nextHeight;
      }

      TransitionImageLayout(cmd, m_Image.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, mipLevels - 1, 1);

      ctx.commandBuffer->EndSingleTimeCommands(cmd);
      staging.Destroy(ctx);
    }
  }

  void VulkanTexture::LoadFromCpuData(const RenderContext& ctx, const CpuTextureData& cpuData, VkFormat format)
  {
    uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(cpuData.width, cpuData.height)))) + 1;

    ImageDesc desc;
    desc.width = cpuData.width;
    desc.height = cpuData.height;
    desc.format = format;
    desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.mipLevels = mipLevels;
    desc.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    SamplerDesc sampler;
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.addressMode = cpuData.repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.anisotropyEnable = true;
    sampler.maxAnisotropy = 16.0f;
    sampler.maxLod = static_cast<float>(mipLevels);

    m_Image.Init(ctx, desc, &sampler);

    if (cpuData.hasCpuMips && cpuData.mips.size() == mipLevels)
    {
      VkDeviceSize totalSize = 0;
      for (auto& mip : cpuData.mips)
        totalSize += static_cast<VkDeviceSize>(mip.width) * mip.height * cpuData.pixelSize;

      VulkanBuffer staging = VulkanBuffer::CreateMapped(ctx, totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
      VkDeviceSize offset = 0;
      for (auto& mip : cpuData.mips)
      {
        uint32_t mipSize = mip.width * mip.height * cpuData.pixelSize;
        staging.Update(static_cast<uint32_t>(offset), mip.data.data(), mipSize);
        offset += mipSize;
      }

      VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();

      TransitionImageLayout(cmd, m_Image.GetImage(),
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels);

      offset = 0;
      for (uint32_t m = 0; m < mipLevels; m++)
      {
        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = m;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { cpuData.mips[m].width, cpuData.mips[m].height, 1 };

        vkCmdCopyBufferToImage(cmd, staging.Get(), m_Image.GetImage(),
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        offset += static_cast<VkDeviceSize>(cpuData.mips[m].width) * cpuData.mips[m].height * cpuData.pixelSize;
      }

      TransitionImageLayout(cmd, m_Image.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels);

      ctx.commandBuffer->EndSingleTimeCommands(cmd);
      staging.Destroy(ctx);
    }
    else
    {
      VkDeviceSize imageSize = static_cast<VkDeviceSize>(cpuData.width) * cpuData.height * cpuData.pixelSize;
      VulkanBuffer staging = VulkanBuffer::CreateMapped(ctx, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
      staging.Update(0, cpuData.mips[0].data.data(), static_cast<uint32_t>(imageSize));

      VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();

      TransitionImageLayout(cmd, m_Image.GetImage(),
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels);

      VkBufferImageCopy region{};
      region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.imageSubresource.mipLevel = 0;
      region.imageSubresource.baseArrayLayer = 0;
      region.imageSubresource.layerCount = 1;
      region.imageExtent = { cpuData.width, cpuData.height, 1 };

      vkCmdCopyBufferToImage(cmd, staging.Get(), m_Image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

      int32_t mipWidth = static_cast<int32_t>(cpuData.width);
      int32_t mipHeight = static_cast<int32_t>(cpuData.height);

      for (uint32_t i = 1; i < mipLevels; i++)
      {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = m_Image.GetImage();
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
          0,
          0, nullptr,
          0, nullptr,
          1, &barrier);

        int32_t nextWidth = mipWidth > 1 ? mipWidth / 2 : 1;
        int32_t nextHeight = mipHeight > 1 ? mipHeight / 2 : 1;

        VkImageBlit blit{};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { nextWidth, nextHeight, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(cmd,
          m_Image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          m_Image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          1, &blit,
          VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          0,
          0, nullptr,
          0, nullptr,
          1, &barrier);

        mipWidth = nextWidth;
        mipHeight = nextHeight;
      }

      TransitionImageLayout(cmd, m_Image.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, mipLevels - 1, 1);

      ctx.commandBuffer->EndSingleTimeCommands(cmd);
      staging.Destroy(ctx);
    }
  }

  void VulkanTexture::Destroy(const RenderContext& ctx)
  {
    m_Image.Destroy(ctx);
  }
}
