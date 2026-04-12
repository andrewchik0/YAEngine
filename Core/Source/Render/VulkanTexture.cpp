#include "VulkanTexture.h"

#include "RenderContext.h"
#include "VulkanBuffer.h"
#include "VulkanCommandBuffer.h"
#include "ImageBarrier.h"

namespace YAEngine
{
  struct CpuMipLevel
  {
    std::vector<uint8_t> data;
    uint32_t width, height;
  };

  static void GenerateMipsWithAlphaCoverage(const uint8_t* srcData, uint32_t width, uint32_t height,
                                            uint32_t mipLevels, std::vector<CpuMipLevel>& outMips)
  {
    outMips.resize(mipLevels);
    outMips[0].width = width;
    outMips[0].height = height;
    outMips[0].data.assign(srcData, srcData + width * height * 4);

    // Compute target alpha coverage from mip 0
    constexpr float alphaCutoff = 128.0f;
    uint32_t passCount = 0;
    uint32_t totalTexels = width * height;
    for (uint32_t i = 0; i < totalTexels; i++)
    {
      if (outMips[0].data[i * 4 + 3] >= static_cast<uint8_t>(alphaCutoff))
        passCount++;
    }
    float targetCoverage = static_cast<float>(passCount) / static_cast<float>(totalTexels);

    for (uint32_t m = 1; m < mipLevels; m++)
    {
      uint32_t srcW = outMips[m - 1].width;
      uint32_t srcH = outMips[m - 1].height;
      uint32_t dstW = std::max(1u, srcW / 2);
      uint32_t dstH = std::max(1u, srcH / 2);

      outMips[m].width = dstW;
      outMips[m].height = dstH;
      outMips[m].data.resize(dstW * dstH * 4);

      auto& src = outMips[m - 1].data;
      auto& dst = outMips[m].data;

      // Box filter: average 2x2 blocks
      for (uint32_t y = 0; y < dstH; y++)
      {
        for (uint32_t x = 0; x < dstW; x++)
        {
          uint32_t sx = x * 2;
          uint32_t sy = y * 2;
          for (uint32_t c = 0; c < 4; c++)
          {
            uint32_t sum = src[(sy * srcW + sx) * 4 + c];
            uint32_t count = 1;
            if (sx + 1 < srcW) { sum += src[(sy * srcW + sx + 1) * 4 + c]; count++; }
            if (sy + 1 < srcH) { sum += src[((sy + 1) * srcW + sx) * 4 + c]; count++; }
            if (sx + 1 < srcW && sy + 1 < srcH) { sum += src[((sy + 1) * srcW + sx + 1) * 4 + c]; count++; }
            dst[(y * dstW + x) * 4 + c] = static_cast<uint8_t>(sum / count);
          }
        }
      }

      // Binary search for alpha scale factor that preserves coverage
      uint32_t mipTexels = dstW * dstH;
      float lo = 0.1f, hi = 8.0f;
      for (int32_t iter = 0; iter < 20; iter++)
      {
        float mid = (lo + hi) * 0.5f;
        uint32_t pass = 0;
        for (uint32_t i = 0; i < mipTexels; i++)
        {
          if (dst[i * 4 + 3] * mid >= alphaCutoff)
            pass++;
        }
        float coverage = static_cast<float>(pass) / static_cast<float>(mipTexels);
        if (coverage < targetCoverage)
          lo = mid;
        else
          hi = mid;
      }

      float scale = (lo + hi) * 0.5f;
      for (uint32_t i = 0; i < mipTexels; i++)
      {
        float a = dst[i * 4 + 3] * scale;
        dst[i * 4 + 3] = static_cast<uint8_t>(std::min(255.0f, std::round(a)));
      }
    }
  }

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
      // CPU mip generation with alpha coverage preservation
      std::vector<CpuMipLevel> mips;
      GenerateMipsWithAlphaCoverage(static_cast<uint8_t*>(inputData), width, height, mipLevels, mips);

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

  void VulkanTexture::Destroy(const RenderContext& ctx)
  {
    m_Image.Destroy(ctx);
  }
}
