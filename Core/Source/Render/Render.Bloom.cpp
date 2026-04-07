#include "Render.h"

#include "BloomData.h"
#include "DebugMarker.h"
#include "ImageBarrier.h"
#include "Utils/Log.h"

namespace YAEngine
{
  void Render::CreateBloomResources()
  {
    auto& ctx = m_Backend.GetContext();
    uint32_t w = m_Graph.GetExtent().width / 2;
    uint32_t h = m_Graph.GetExtent().height / 2;

    uint32_t mipCount = BLOOM_MIP_COUNT;

    // Create bloom image with mip chain
    ImageDesc bloomDesc;
    bloomDesc.width = w;
    bloomDesc.height = h;
    bloomDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    bloomDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    bloomDesc.mipLevels = mipCount;

    // Sampler with linear filtering across mips
    SamplerDesc sampDesc;
    sampDesc.magFilter = VK_FILTER_LINEAR;
    sampDesc.minFilter = VK_FILTER_LINEAR;
    sampDesc.addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampDesc.maxLod = static_cast<float>(mipCount);

    m_BloomImage.Init(ctx, bloomDesc, &sampDesc);

    // Clear to black so reads when bloom is disabled don't produce undefined values
    auto clearCmd = m_Backend.GetCommandBuffer().BeginSingleTimeCommands();
    TransitionImageLayout(clearCmd, m_BloomImage.GetImage(),
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount);
    VkClearColorValue clearColor = {{ 0.0f, 0.0f, 0.0f, 0.0f }};
    VkImageSubresourceRange clearRange{};
    clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    clearRange.baseMipLevel = 0;
    clearRange.levelCount = mipCount;
    clearRange.baseArrayLayer = 0;
    clearRange.layerCount = 1;
    vkCmdClearColorImage(clearCmd, m_BloomImage.GetImage(),
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &clearRange);
    TransitionImageLayout(clearCmd, m_BloomImage.GetImage(),
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount);
    m_Backend.GetCommandBuffer().EndSingleTimeCommands(clearCmd);
    m_BloomImage.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    YA_DEBUG_NAME(ctx.device, VK_OBJECT_TYPE_IMAGE,
      m_BloomImage.GetImage(), "Bloom Mip Chain");

    // Create per-mip image views
    m_BloomMipViews.resize(mipCount);
    for (uint32_t mip = 0; mip < mipCount; mip++)
    {
      VkImageViewCreateInfo viewInfo{};
      viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      viewInfo.image = m_BloomImage.GetImage();
      viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
      viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      viewInfo.subresourceRange.baseMipLevel = mip;
      viewInfo.subresourceRange.levelCount = 1;
      viewInfo.subresourceRange.baseArrayLayer = 0;
      viewInfo.subresourceRange.layerCount = 1;

      if (vkCreateImageView(ctx.device, &viewInfo, nullptr, &m_BloomMipViews[mip]) != VK_SUCCESS)
      {
        YA_LOG_ERROR("Render", "Failed to create bloom mip view %d", mip);
        throw std::runtime_error("Failed to create bloom mip view!");
      }

      YA_DEBUG_NAMEF(ctx.device, VK_OBJECT_TYPE_IMAGE_VIEW,
        m_BloomMipViews[mip], "Bloom Mip %u", mip);
    }

    // Downsample descriptor sets: one per mip level
    // set 0: binding 0 = src sampler, binding 1 = dst storage image
    SetDescription bloomSetDesc = {
      .set = 0,
      .bindings = {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT }
      }
    };

    m_BloomDownsampleDescriptorSets.resize(mipCount);
    for (uint32_t mip = 0; mip < mipCount; mip++)
    {
      if (mip == 0)
        m_BloomDownsampleDescriptorSets[mip].Init(ctx, bloomSetDesc);
      else
        m_BloomDownsampleDescriptorSets[mip].Init(ctx, m_BloomDownsampleDescriptorSets[0].GetLayout());
    }

    // Upsample descriptor sets: one per upsample step (mipCount - 1)
    m_BloomUpsampleDescriptorSets.resize(mipCount - 1);
    for (uint32_t i = 0; i < mipCount - 1; i++)
    {
      if (i == 0)
        m_BloomUpsampleDescriptorSets[i].Init(ctx, bloomSetDesc);
      else
        m_BloomUpsampleDescriptorSets[i].Init(ctx, m_BloomUpsampleDescriptorSets[0].GetLayout());
    }

    // Write downsample descriptors
    // Mip 0: reads SSR color (stable RG resource view), writes bloom mip 0
    // Mip 1+: reads bloom full image via sampler (specific mip via textureLod), writes bloom mip N
    auto& ssrColor = m_Graph.GetResource(m_SSRColor);
    for (uint32_t mip = 0; mip < mipCount; mip++)
    {
      if (mip == 0)
      {
        m_BloomDownsampleDescriptorSets[mip].WriteCombinedImageSampler(0,
          ssrColor.GetView(), ssrColor.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      }
      else
      {
        m_BloomDownsampleDescriptorSets[mip].WriteCombinedImageSampler(0,
          m_BloomImage.GetView(), m_BloomImage.GetSampler(), VK_IMAGE_LAYOUT_GENERAL);
      }
      m_BloomDownsampleDescriptorSets[mip].WriteStorageImage(1,
        m_BloomMipViews[mip], VK_IMAGE_LAYOUT_GENERAL);
    }

    // Write upsample descriptors
    // Step i reads bloom mip (mipCount-1-i) via sampler, writes to bloom mip (mipCount-2-i)
    for (uint32_t i = 0; i < mipCount - 1; i++)
    {
      uint32_t dstMip = mipCount - 2 - i;

      m_BloomUpsampleDescriptorSets[i].WriteCombinedImageSampler(0,
        m_BloomImage.GetView(), m_BloomImage.GetSampler(), VK_IMAGE_LAYOUT_GENERAL);
      m_BloomUpsampleDescriptorSets[i].WriteStorageImage(1,
        m_BloomMipViews[dstMip], VK_IMAGE_LAYOUT_GENERAL);
    }

    // Bloom read descriptor set for tonemap pass (set 3: bloom sampler)
    SetDescription bloomReadDesc = {
      .set = 3,
      .bindings = {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
      }
    };
    m_BloomReadDescriptorSet.Init(ctx, bloomReadDesc);
    m_BloomReadDescriptorSet.WriteCombinedImageSampler(0,
      m_BloomMipViews[0], m_BloomImage.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }

  void Render::DestroyBloomResources()
  {
    auto& ctx = m_Backend.GetContext();

    m_BloomReadDescriptorSet.Destroy();

    for (auto& set : m_BloomDownsampleDescriptorSets)
      set.Destroy();
    m_BloomDownsampleDescriptorSets.clear();

    for (auto& set : m_BloomUpsampleDescriptorSets)
      set.Destroy();
    m_BloomUpsampleDescriptorSets.clear();

    for (auto view : m_BloomMipViews)
    {
      if (view != VK_NULL_HANDLE)
        vkDestroyImageView(ctx.device, view, nullptr);
    }
    m_BloomMipViews.clear();

    m_BloomImage.Destroy(ctx);
  }
}
