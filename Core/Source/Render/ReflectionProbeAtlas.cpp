#include "ReflectionProbeAtlas.h"

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
  void ReflectionProbeAtlas::Init(const RenderContext& ctx)
  {
    m_MaxSlots = MAX_REFLECTION_PROBES + 1; // slot 0 = skybox
    uint32_t totalLayers = m_MaxSlots * 6; // 6 faces per cubemap

    // Irradiance cubemap array: ONE slot, the skybox. Probes are specular only,
    // diffuse indirect comes from irradiance volumes, and slot 0 is what the
    // shader falls back to outside every volume.
    {
      ImageDesc desc {
        .width = PROBE_IRRADIANCE_SIZE,
        .height = PROBE_IRRADIANCE_SIZE,
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .mipLevels = 1,
        .arrayLayers = 6,
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

    // Prefilter cubemap array: 256x256, 9 mips, all slots
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
      VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);

    TransitionImageLayout(cmd, m_Prefilter.GetImage(),
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, PROBE_PREFILTER_MIP_LEVELS, 0, totalLayers);

    ctx.commandBuffer->EndSingleTimeCommands(cmd);

    m_Irradiance.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_Prefilter.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    YA_LOG_INFO("Render", "Reflection probe atlas initialized (%u slots)", m_MaxSlots);
  }

  void ReflectionProbeAtlas::Destroy(const RenderContext& ctx)
  {
#ifdef YA_EDITOR
    DestroyPreviews(ctx);
#endif
    m_Irradiance.Destroy(ctx);
    m_Prefilter.Destroy(ctx);
  }

  void ReflectionProbeAtlas::CopyCubeFaces(VkCommandBuffer cmd, VkImage srcImage,
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

  void ReflectionProbeAtlas::UploadSkybox(const RenderContext& ctx, VulkanCubicTexture& skybox)
  {
    VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();

    // -- Irradiance, slot 0 only --
    TransitionImageLayout(cmd, skybox.GetIrradianceImage(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);

    TransitionImageLayout(cmd, m_Irradiance.GetImage(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);

    CopyCubeFaces(cmd, skybox.GetIrradianceImage(), m_Irradiance.GetImage(),
      0, PROBE_IRRADIANCE_SIZE, PROBE_IRRADIANCE_SIZE, 1);

    TransitionImageLayout(cmd, skybox.GetIrradianceImage(),
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);

    TransitionImageLayout(cmd, m_Irradiance.GetImage(),
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);

    // -- Prefilter, slot 0 --
    TransitionImageLayout(cmd, skybox.GetPrefilterImage(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, PROBE_PREFILTER_MIP_LEVELS, 0, 6);

    TransitionImageLayout(cmd, m_Prefilter.GetImage(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, PROBE_PREFILTER_MIP_LEVELS, 0, 6);

    CopyCubeFaces(cmd, skybox.GetPrefilterImage(), m_Prefilter.GetImage(),
      0, CUBEMAP_SIZE, PROBE_PREFILTER_SIZE, PROBE_PREFILTER_MIP_LEVELS);

    TransitionImageLayout(cmd, skybox.GetPrefilterImage(),
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, PROBE_PREFILTER_MIP_LEVELS, 0, 6);

    TransitionImageLayout(cmd, m_Prefilter.GetImage(),
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, PROBE_PREFILTER_MIP_LEVELS, 0, 6);

    ctx.commandBuffer->EndSingleTimeCommands(cmd);
  }

  bool ReflectionProbeAtlas::UploadPrefilterFromData(const RenderContext& ctx, uint32_t slotIndex,
    const CubeMapFileData& data)
  {
    const uint32_t pfWidth = data.width;

    uint32_t maxMipLevels = 1;
    for (uint32_t dimension = pfWidth; dimension > 1; dimension /= 2)
      maxMipLevels++;

    // The temp image below is hard-coded to R16G16B16A16_SFLOAT (8 bytes per texel)
    // and every copy region is derived from these fields, so a file that disagrees
    // would over-read the staging buffer rather than merely look wrong.
    const bool valid = slotIndex < m_MaxSlots
      && data.faceCount == 6
      && pfWidth > 0
      && data.height == pfWidth
      && data.bytesPerPixel == 8
      && data.vkFormat == uint32_t(VK_FORMAT_R16G16B16A16_SFLOAT)
      && data.mipLevels >= 1
      && data.mipLevels <= maxMipLevels
      && data.pixels.size() == CubeMapFile::GetTotalSize(pfWidth, data.height,
           data.faceCount, data.mipLevels, data.bytesPerPixel);

    if (!valid)
    {
      YA_LOG_ERROR("Render", "Prefilter cubemap does not fit the probe atlas: slot %u, %ux%u, %u faces, %u mips, %u bytes per texel, format %u, %zu bytes",
        slotIndex, pfWidth, data.height, data.faceCount, data.mipLevels,
        data.bytesPerPixel, data.vkFormat, data.pixels.size());
      return false;
    }

    uint32_t baseLayer = slotIndex * 6;

    // Upload prefilter via staging image (handles size mismatch with atlas)
    uint32_t copyMips = std::min(data.mipLevels, PROBE_PREFILTER_MIP_LEVELS);

    VulkanImage temp;
    temp.Init(ctx, {
      .width = pfWidth, .height = pfWidth,
      .format = VK_FORMAT_R16G16B16A16_SFLOAT,
      .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      .mipLevels = copyMips,
      .arrayLayers = 6,
      .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
      .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
    });

    // The whole payload is staged, not just the mips that are copied: the face
    // stride in the file is set by ITS mip count, so trimming the buffer to
    // copyMips would put every face past the first at the wrong offset.
    auto staging = VulkanBuffer::CreateStaged(ctx, data.pixels.data(), data.pixels.size(),
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();

    TransitionImageLayout(cmd, temp.GetImage(),
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, copyMips, 0, 6);

    for (uint32_t face = 0; face < 6; face++)
    {
      for (uint32_t mip = 0; mip < copyMips; mip++)
      {
        uint32_t mipSize = std::max(1u, pfWidth >> mip);
        VkBufferImageCopy region {};
        region.bufferOffset = CubeMapFile::GetMipOffset(face, mip, pfWidth, pfWidth,
          data.mipLevels, data.bytesPerPixel);
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, face, 1 };
        region.imageExtent = { mipSize, mipSize, 1 };
        vkCmdCopyBufferToImage(cmd, staging.Get(), temp.GetImage(),
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
      }
    }

    TransitionImageLayout(cmd, temp.GetImage(),
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, copyMips, 0, 6);

    TransitionImageLayout(cmd, m_Prefilter.GetImage(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, PROBE_PREFILTER_MIP_LEVELS, baseLayer, 6);

    CopyCubeFaces(cmd, temp.GetImage(), m_Prefilter.GetImage(),
      slotIndex, pfWidth, PROBE_PREFILTER_SIZE, copyMips);

    // Fill remaining atlas mips by reusing the last source mip
    if (copyMips < PROBE_PREFILTER_MIP_LEVELS)
    {
      uint32_t lastMip = copyMips - 1;
      uint32_t lastSrcSize = std::max(1u, pfWidth >> lastMip);

      for (uint32_t mip = copyMips; mip < PROBE_PREFILTER_MIP_LEVELS; mip++)
      {
        uint32_t dstMipSize = std::max(1u, PROBE_PREFILTER_SIZE >> mip);

        for (uint32_t face = 0; face < 6; face++)
        {
          VkImageBlit blit {};
          blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, lastMip, face, 1 };
          blit.srcOffsets[1] = { int32_t(lastSrcSize), int32_t(lastSrcSize), 1 };
          blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, baseLayer + face, 1 };
          blit.dstOffsets[1] = { int32_t(dstMipSize), int32_t(dstMipSize), 1 };

          vkCmdBlitImage(cmd,
            temp.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            m_Prefilter.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);
        }
      }
    }

    TransitionImageLayout(cmd, m_Prefilter.GetImage(),
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, PROBE_PREFILTER_MIP_LEVELS, baseLayer, 6);

    ctx.commandBuffer->EndSingleTimeCommands(cmd);
    staging.Destroy(ctx);
    temp.Destroy(ctx);

    YA_LOG_INFO("Render", "Uploaded probe prefilter to atlas slot %u from disk", slotIndex);
    return true;
  }

#ifdef YA_EDITOR
  VkDescriptorSet ReflectionProbeAtlas::CreateFacePreview(const RenderContext& ctx,
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

  VkDescriptorSet ReflectionProbeAtlas::GetPrefilterFacePreview(const RenderContext& ctx, uint32_t slot, uint32_t face)
  {
    assert(face < 6);
    return CreateFacePreview(ctx, m_Prefilter, slot, face, m_Previews[slot].prefilter[face]);
  }

  static void DestroyFacePreview(VkDevice device, ReflectionProbeAtlas::FacePreview& fp)
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

  void ReflectionProbeAtlas::InvalidateSlotPreview(const RenderContext& ctx, uint32_t slot)
  {
    auto it = m_Previews.find(slot);
    if (it == m_Previews.end())
      return;

    for (uint32_t f = 0; f < 6; f++)
      DestroyFacePreview(ctx.device, it->second.prefilter[f]);
    m_Previews.erase(it);
  }

  void ReflectionProbeAtlas::DestroyPreviews(const RenderContext& ctx)
  {
    for (auto& [slot, preview] : m_Previews)
    {
      for (uint32_t f = 0; f < 6; f++)
        DestroyFacePreview(ctx.device, preview.prefilter[f]);
    }
    m_Previews.clear();
  }
#endif
}
