#include "StagingBatchUploader.h"

#include "RenderContext.h"
#include "VulkanCommandBuffer.h"
#include "ImageBarrier.h"
#include "Utils/Log.h"

namespace YAEngine
{
  uint32_t StagingBatchUploader::AddTexture(CpuTextureData&& cpuData, VkFormat format)
  {
    uint32_t index = static_cast<uint32_t>(m_Textures.size());
    uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(cpuData.width, cpuData.height)))) + 1;
    m_Textures.push_back({ std::move(cpuData), {}, format, mipLevels });
    return index;
  }

  uint32_t StagingBatchUploader::AddBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage)
  {
    uint32_t index = static_cast<uint32_t>(m_Buffers.size());
    PendingBuffer pb;
    pb.data.resize(size);
    std::memcpy(pb.data.data(), data, size);
    pb.usage = usage;
    m_Buffers.push_back(std::move(pb));
    return index;
  }

  void StagingBatchUploader::Flush(const RenderContext& ctx)
  {
    if (m_Textures.empty() && m_Buffers.empty())
      return;

    // Compute total staging size
    VkDeviceSize totalStagingSize = 0;

    for (auto& tex : m_Textures)
    {
      if (tex.cpuData.hasCpuMips)
      {
        for (auto& mip : tex.cpuData.mips)
          totalStagingSize += static_cast<VkDeviceSize>(mip.width) * mip.height * tex.cpuData.pixelSize;
      }
      else
      {
        totalStagingSize += static_cast<VkDeviceSize>(tex.cpuData.width) * tex.cpuData.height * tex.cpuData.pixelSize;
      }
    }

    for (auto& buf : m_Buffers)
      totalStagingSize += buf.data.size();

    if (totalStagingSize > std::numeric_limits<uint32_t>::max())
    {
      YA_LOG_ERROR("Render", "StagingBatchUploader: total staging size %llu exceeds uint32_t range",
        static_cast<unsigned long long>(totalStagingSize));
      return;
    }

    // Create staging buffer
    VulkanBuffer staging = VulkanBuffer::CreateMapped(ctx, totalStagingSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    // Create all GPU resources + copy data to staging
    VkDeviceSize stagingOffset = 0;

    struct TextureStagingInfo
    {
      VkDeviceSize baseOffset;
      std::vector<VkDeviceSize> mipOffsets;
    };
    std::vector<TextureStagingInfo> texStagingInfo(m_Textures.size());

    for (size_t i = 0; i < m_Textures.size(); i++)
    {
      auto& tex = m_Textures[i];

      ImageDesc desc;
      desc.width = tex.cpuData.width;
      desc.height = tex.cpuData.height;
      desc.format = tex.format;
      desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
      desc.mipLevels = tex.mipLevels;
      desc.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

      SamplerDesc sampler;
      sampler.magFilter = VK_FILTER_LINEAR;
      sampler.minFilter = VK_FILTER_LINEAR;
      sampler.addressMode = tex.cpuData.repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      sampler.anisotropyEnable = true;
      sampler.maxAnisotropy = 16.0f;
      sampler.maxLod = static_cast<float>(tex.mipLevels);

      tex.image.Init(ctx, desc, &sampler);

      texStagingInfo[i].baseOffset = stagingOffset;

      if (tex.cpuData.hasCpuMips)
      {
        for (auto& mip : tex.cpuData.mips)
        {
          uint32_t mipSize = mip.width * mip.height * tex.cpuData.pixelSize;
          texStagingInfo[i].mipOffsets.push_back(stagingOffset);
          staging.Update(static_cast<uint32_t>(stagingOffset), mip.data.data(), mipSize);
          stagingOffset += mipSize;
        }
      }
      else
      {
        uint32_t baseSize = tex.cpuData.width * tex.cpuData.height * tex.cpuData.pixelSize;
        texStagingInfo[i].mipOffsets.push_back(stagingOffset);
        staging.Update(static_cast<uint32_t>(stagingOffset), tex.cpuData.mips[0].data.data(), baseSize);
        stagingOffset += baseSize;
      }
    }

    struct BufferStagingInfo
    {
      VkDeviceSize offset;
    };
    std::vector<BufferStagingInfo> bufStagingInfo(m_Buffers.size());

    for (size_t i = 0; i < m_Buffers.size(); i++)
    {
      auto& buf = m_Buffers[i];
      buf.buffer = VulkanBuffer::CreateGpuOnly(ctx, buf.data.size(), buf.usage);

      bufStagingInfo[i].offset = stagingOffset;
      staging.Update(static_cast<uint32_t>(stagingOffset), buf.data.data(),
        static_cast<uint32_t>(buf.data.size()));
      stagingOffset += buf.data.size();
    }

    // Record all commands
    VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();

    // Transition all images to TRANSFER_DST_OPTIMAL
    for (size_t i = 0; i < m_Textures.size(); i++)
    {
      TransitionImageLayout(cmd, m_Textures[i].image.GetImage(),
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, m_Textures[i].mipLevels);
    }

    // Copy texture data from staging to images
    for (size_t i = 0; i < m_Textures.size(); i++)
    {
      auto& tex = m_Textures[i];
      auto& info = texStagingInfo[i];

      if (tex.cpuData.hasCpuMips)
      {
        for (uint32_t m = 0; m < tex.mipLevels; m++)
        {
          VkBufferImageCopy region{};
          region.bufferOffset = info.mipOffsets[m];
          region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
          region.imageSubresource.mipLevel = m;
          region.imageSubresource.baseArrayLayer = 0;
          region.imageSubresource.layerCount = 1;
          region.imageExtent = { tex.cpuData.mips[m].width, tex.cpuData.mips[m].height, 1 };

          vkCmdCopyBufferToImage(cmd, staging.Get(), tex.image.GetImage(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        }
      }
      else
      {
        VkBufferImageCopy region{};
        region.bufferOffset = info.mipOffsets[0];
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { tex.cpuData.width, tex.cpuData.height, 1 };

        vkCmdCopyBufferToImage(cmd, staging.Get(), tex.image.GetImage(),
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
      }
    }

    // Generate GPU mipchains for textures without CPU mips
    for (size_t i = 0; i < m_Textures.size(); i++)
    {
      auto& tex = m_Textures[i];
      if (tex.cpuData.hasCpuMips)
        continue;

      int32_t mipWidth = static_cast<int32_t>(tex.cpuData.width);
      int32_t mipHeight = static_cast<int32_t>(tex.cpuData.height);

      for (uint32_t m = 1; m < tex.mipLevels; m++)
      {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = tex.image.GetImage();
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = m - 1;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
          0, 0, nullptr, 0, nullptr, 1, &barrier);

        int32_t nextWidth = mipWidth > 1 ? mipWidth / 2 : 1;
        int32_t nextHeight = mipHeight > 1 ? mipHeight / 2 : 1;

        VkImageBlit blit{};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = m - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { nextWidth, nextHeight, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = m;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(cmd,
          tex.image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          tex.image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          1, &blit, VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          0, 0, nullptr, 0, nullptr, 1, &barrier);

        mipWidth = nextWidth;
        mipHeight = nextHeight;
      }
    }

    // Final transition for all textures
    for (size_t i = 0; i < m_Textures.size(); i++)
    {
      auto& tex = m_Textures[i];
      if (tex.cpuData.hasCpuMips)
      {
        // All mips in TRANSFER_DST -> SHADER_READ_ONLY
        TransitionImageLayout(cmd, tex.image.GetImage(),
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_IMAGE_ASPECT_COLOR_BIT, 0, tex.mipLevels);
      }
      else
      {
        // Only last mip still in TRANSFER_DST (others already transitioned during blit)
        TransitionImageLayout(cmd, tex.image.GetImage(),
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_IMAGE_ASPECT_COLOR_BIT, tex.mipLevels - 1, 1);
      }
    }

    // Copy buffer data from staging
    for (size_t i = 0; i < m_Buffers.size(); i++)
    {
      VkBufferCopy copyRegion{};
      copyRegion.srcOffset = bufStagingInfo[i].offset;
      copyRegion.size = m_Buffers[i].data.size();
      vkCmdCopyBuffer(cmd, staging.Get(), m_Buffers[i].buffer.Get(), 1, &copyRegion);
    }

    ctx.commandBuffer->EndSingleTimeCommands(cmd);
    staging.Destroy(ctx);

    // Free CPU data
    for (auto& tex : m_Textures)
      tex.cpuData.mips.clear();
    for (auto& buf : m_Buffers)
      buf.data.clear();
  }

  void StagingBatchUploader::Destroy(const RenderContext& ctx)
  {
    for (auto& tex : m_Textures)
      tex.image.Destroy(ctx);
    for (auto& buf : m_Buffers)
      buf.buffer.Destroy(ctx);
    m_Textures.clear();
    m_Buffers.clear();
  }

  void StagingBatchUploader::Clear()
  {
    m_Textures.clear();
    m_Buffers.clear();
  }
}
