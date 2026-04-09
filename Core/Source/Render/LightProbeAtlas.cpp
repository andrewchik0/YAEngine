#include "LightProbeAtlas.h"

#include "RenderContext.h"
#include "VulkanCommandBuffer.h"
#include "VulkanCubicTexture.h"
#include "VulkanBuffer.h"
#include "ImageBarrier.h"
#include "Assets/CubeMapFile.h"
#include "Utils/Log.h"

#ifdef YA_EDITOR
#include <ImGui/imgui_impl_vulkan.h>
#endif

namespace YAEngine
{
  void LightProbeAtlas::Init(const RenderContext& ctx)
  {
    m_MaxSlots = MAX_LIGHT_PROBES + 1; // slot 0 = skybox
    uint32_t totalLayers = m_MaxSlots * 6; // 6 faces per cubemap

    // Irradiance cubemap array: 32x32, 1 mip, all slots
    {
      ImageDesc desc {
        .width = PROBE_IRRADIANCE_SIZE,
        .height = PROBE_IRRADIANCE_SIZE,
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .mipLevels = 1,
        .arrayLayers = totalLayers,
        .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY,
      };
      SamplerDesc sampler {
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 0.0f,
      };
      m_Irradiance.Init(ctx, desc, &sampler);
    }

    // Prefilter cubemap array: 128x128, 8 mips, all slots
    {
      ImageDesc desc {
        .width = PROBE_PREFILTER_SIZE,
        .height = PROBE_PREFILTER_SIZE,
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .mipLevels = PROBE_PREFILTER_MIP_LEVELS,
        .arrayLayers = totalLayers,
        .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY,
      };
      SamplerDesc sampler {
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = float(PROBE_PREFILTER_MIP_LEVELS - 1),
      };
      m_Prefilter.Init(ctx, desc, &sampler);
    }

    // Transition both to SHADER_READ_ONLY as initial state
    VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();

    TransitionImageLayout(cmd, m_Irradiance.GetImage(),
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, totalLayers);

    TransitionImageLayout(cmd, m_Prefilter.GetImage(),
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, PROBE_PREFILTER_MIP_LEVELS, 0, totalLayers);

    ctx.commandBuffer->EndSingleTimeCommands(cmd);

    m_Irradiance.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_Prefilter.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    YA_LOG_INFO("Render", "Light probe atlas initialized (%u slots)", m_MaxSlots);
  }

  void LightProbeAtlas::Destroy(const RenderContext& ctx)
  {
#ifdef YA_EDITOR
    DestroyPreviews(ctx);
#endif
    m_Irradiance.Destroy(ctx);
    m_Prefilter.Destroy(ctx);
  }

  void LightProbeAtlas::CopyCubeFaces(VkCommandBuffer cmd, VkImage srcImage,
    VkImage dstImage, uint32_t slotIndex,
    uint32_t srcSize, uint32_t dstSize,
    uint32_t mipLevels)
  {
    uint32_t baseLayer = slotIndex * 6;

    for (uint32_t mip = 0; mip < mipLevels; mip++)
    {
      uint32_t srcMipSize = std::max(1u, srcSize >> mip);
      uint32_t dstMipSize = std::max(1u, dstSize >> mip);

      for (uint32_t face = 0; face < 6; face++)
      {
        VkImageBlit blitRegion {};
        blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.srcSubresource.mipLevel = mip;
        blitRegion.srcSubresource.baseArrayLayer = face;
        blitRegion.srcSubresource.layerCount = 1;
        blitRegion.srcOffsets[0] = { 0, 0, 0 };
        blitRegion.srcOffsets[1] = { int32_t(srcMipSize), int32_t(srcMipSize), 1 };

        blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.dstSubresource.mipLevel = mip;
        blitRegion.dstSubresource.baseArrayLayer = baseLayer + face;
        blitRegion.dstSubresource.layerCount = 1;
        blitRegion.dstOffsets[0] = { 0, 0, 0 };
        blitRegion.dstOffsets[1] = { int32_t(dstMipSize), int32_t(dstMipSize), 1 };

        vkCmdBlitImage(cmd,
          srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          1, &blitRegion, VK_FILTER_LINEAR);
      }
    }
  }

  void LightProbeAtlas::UploadSlot(const RenderContext& ctx, uint32_t slotIndex, VulkanCubicTexture& src)
  {
    assert(slotIndex < m_MaxSlots);

    VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();

    uint32_t baseLayer = slotIndex * 6;

    // -- Irradiance --
    // Transition source irradiance: SHADER_READ -> TRANSFER_SRC
    TransitionImageLayout(cmd, src.GetIrradianceImage(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);

    // Transition destination slot: SHADER_READ -> TRANSFER_DST
    TransitionImageLayout(cmd, m_Irradiance.GetImage(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, baseLayer, 6);

    CopyCubeFaces(cmd, src.GetIrradianceImage(), m_Irradiance.GetImage(),
      slotIndex, PROBE_IRRADIANCE_SIZE, PROBE_IRRADIANCE_SIZE, 1);

    // Transition back
    TransitionImageLayout(cmd, src.GetIrradianceImage(),
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);

    TransitionImageLayout(cmd, m_Irradiance.GetImage(),
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, baseLayer, 6);

    // -- Prefilter --
    uint32_t srcMipLevels = PROBE_PREFILTER_MIP_LEVELS;
    uint32_t copyMips = PROBE_PREFILTER_MIP_LEVELS;

    // Transition source prefilter: SHADER_READ -> TRANSFER_SRC
    TransitionImageLayout(cmd, src.GetPrefilterImage(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, srcMipLevels, 0, 6);

    // Transition destination slot: SHADER_READ -> TRANSFER_DST
    TransitionImageLayout(cmd, m_Prefilter.GetImage(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, PROBE_PREFILTER_MIP_LEVELS, baseLayer, 6);

    CopyCubeFaces(cmd, src.GetPrefilterImage(), m_Prefilter.GetImage(),
      slotIndex, CUBEMAP_SIZE, PROBE_PREFILTER_SIZE, copyMips);

    // Transition back
    TransitionImageLayout(cmd, src.GetPrefilterImage(),
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, srcMipLevels, 0, 6);

    TransitionImageLayout(cmd, m_Prefilter.GetImage(),
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, PROBE_PREFILTER_MIP_LEVELS, baseLayer, 6);

    ctx.commandBuffer->EndSingleTimeCommands(cmd);
  }

  void LightProbeAtlas::UploadSlotFromData(const RenderContext& ctx, uint32_t slotIndex,
    const void* irradianceData, uint32_t irrWidth,
    const void* prefilterData, uint32_t pfWidth, uint32_t pfMipLevels,
    uint32_t bytesPerPixel)
  {
    assert(slotIndex < m_MaxSlots);
    uint32_t baseLayer = slotIndex * 6;

    // Upload irradiance
    {
      size_t totalSize = CubeMapFile::GetTotalSize(irrWidth, irrWidth, 6, 1, bytesPerPixel);
      auto staging = VulkanBuffer::CreateStaged(ctx, irradianceData, totalSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

      VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();

      TransitionImageLayout(cmd, m_Irradiance.GetImage(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, baseLayer, 6);

      for (uint32_t face = 0; face < 6; face++)
      {
        VkBufferImageCopy region {};
        region.bufferOffset = CubeMapFile::GetMipOffset(face, 0, irrWidth, irrWidth, 1, bytesPerPixel);
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, baseLayer + face, 1 };
        region.imageExtent = { irrWidth, irrWidth, 1 };
        vkCmdCopyBufferToImage(cmd, staging.Get(), m_Irradiance.GetImage(),
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
      }

      TransitionImageLayout(cmd, m_Irradiance.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, baseLayer, 6);

      ctx.commandBuffer->EndSingleTimeCommands(cmd);
      staging.Destroy(ctx);
    }

    // Upload prefilter
    {
      uint32_t copyMips = std::min(pfMipLevels, PROBE_PREFILTER_MIP_LEVELS);
      size_t totalSize = CubeMapFile::GetTotalSize(pfWidth, pfWidth, 6, copyMips, bytesPerPixel);
      auto staging = VulkanBuffer::CreateStaged(ctx, prefilterData, totalSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

      VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();

      TransitionImageLayout(cmd, m_Prefilter.GetImage(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, PROBE_PREFILTER_MIP_LEVELS, baseLayer, 6);

      for (uint32_t face = 0; face < 6; face++)
      {
        for (uint32_t mip = 0; mip < copyMips; mip++)
        {
          uint32_t mipSize = std::max(1u, pfWidth >> mip);
          VkBufferImageCopy region {};
          region.bufferOffset = CubeMapFile::GetMipOffset(face, mip, pfWidth, pfWidth, copyMips, bytesPerPixel);
          region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, baseLayer + face, 1 };
          region.imageExtent = { mipSize, mipSize, 1 };
          vkCmdCopyBufferToImage(cmd, staging.Get(), m_Prefilter.GetImage(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        }
      }

      TransitionImageLayout(cmd, m_Prefilter.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, PROBE_PREFILTER_MIP_LEVELS, baseLayer, 6);

      ctx.commandBuffer->EndSingleTimeCommands(cmd);
      staging.Destroy(ctx);
    }

    YA_LOG_INFO("Render", "Uploaded probe data to atlas slot %u from disk", slotIndex);
  }

  void LightProbeAtlas::UploadSkybox(const RenderContext& ctx, VulkanCubicTexture& skybox)
  {
    UploadSlot(ctx, 0, skybox);
  }

#ifdef YA_EDITOR
  VkDescriptorSet LightProbeAtlas::CreateFacePreview(const RenderContext& ctx,
    VulkanImage& image, uint32_t slot, uint32_t face, FacePreview& out)
  {
    if (out.descriptor != VK_NULL_HANDLE)
      return out.descriptor;

    VkImageViewCreateInfo viewInfo {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image.GetImage();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = slot * 6 + face;
    viewInfo.subresourceRange.layerCount = 1;
    vkCreateImageView(ctx.device, &viewInfo, nullptr, &out.view);

    out.descriptor = ImGui_ImplVulkan_AddTexture(
      image.GetSampler(), out.view,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    return out.descriptor;
  }

  VkDescriptorSet LightProbeAtlas::GetPrefilterFacePreview(const RenderContext& ctx, uint32_t slot, uint32_t face)
  {
    assert(face < 6);
    return CreateFacePreview(ctx, m_Prefilter, slot, face, m_Previews[slot].prefilter[face]);
  }

  VkDescriptorSet LightProbeAtlas::GetIrradianceFacePreview(const RenderContext& ctx, uint32_t slot, uint32_t face)
  {
    assert(face < 6);
    return CreateFacePreview(ctx, m_Irradiance, slot, face, m_Previews[slot].irradiance[face]);
  }

  static void DestroyFacePreview(VkDevice device, LightProbeAtlas::FacePreview& fp)
  {
    if (fp.descriptor != VK_NULL_HANDLE)
    {
      ImGui_ImplVulkan_RemoveTexture(fp.descriptor);
      fp.descriptor = VK_NULL_HANDLE;
    }
    if (fp.view != VK_NULL_HANDLE)
    {
      vkDestroyImageView(device, fp.view, nullptr);
      fp.view = VK_NULL_HANDLE;
    }
  }

  void LightProbeAtlas::InvalidateSlotPreview(const RenderContext& ctx, uint32_t slot)
  {
    auto it = m_Previews.find(slot);
    if (it == m_Previews.end())
      return;

    for (uint32_t f = 0; f < 6; f++)
    {
      DestroyFacePreview(ctx.device, it->second.prefilter[f]);
      DestroyFacePreview(ctx.device, it->second.irradiance[f]);
    }
    m_Previews.erase(it);
  }

  void LightProbeAtlas::DestroyPreviews(const RenderContext& ctx)
  {
    for (auto& [slot, preview] : m_Previews)
    {
      for (uint32_t f = 0; f < 6; f++)
      {
        DestroyFacePreview(ctx.device, preview.prefilter[f]);
        DestroyFacePreview(ctx.device, preview.irradiance[f]);
      }
    }
    m_Previews.clear();
  }
#endif
}
