#ifdef YA_EDITOR

#include "ReflectionProbeBaker.h"

#include "Render.h"
#include "RenderContext.h"
#include "VulkanCommandBuffer.h"
#include "VulkanBuffer.h"
#include "ImageBarrier.h"
#include "CubemapConvolution.h"
#include "Assets/CubeMapFile.h"
#include "FrameContext.h"
#include "Utils/Log.h"

namespace YAEngine
{
  void ReflectionProbeBaker::Init(Render& render, uint32_t resolution)
  {
    m_Render = &render;
    m_Ctx = &render.GetContext();
    m_OffscreenRenderer.Init(render, resolution);
    m_Resolution = resolution;
  }

  void ReflectionProbeBaker::Destroy()
  {
    m_OffscreenRenderer.Destroy();
    m_Ctx = nullptr;
    m_Render = nullptr;
    m_Resolution = 0;
  }

  void ReflectionProbeBaker::EnsureResolution(uint32_t resolution)
  {
    if (m_Resolution == resolution) return;

    vkDeviceWaitIdle(m_Ctx->device);
    m_OffscreenRenderer.Destroy();
    m_OffscreenRenderer.Init(*m_Render, resolution);
    m_Resolution = resolution;
  }

  void ReflectionProbeBaker::Bake(CubicTextureResources& cubicRes,
    FrameContext& frame, ReflectionProbeAtlas& atlas,
    glm::vec3 position, uint32_t resolution, uint32_t atlasSlot,
    const std::string& prefilterSavePath)
  {
    // Clamped before the log line: an out-of-range resolution is exactly the case
    // where the log matters.
    resolution = std::clamp(resolution,
      BakeLimits::PROBE_MIN_CAPTURE_RESOLUTION, BakeLimits::PROBE_MAX_CAPTURE_RESOLUTION);

    YA_LOG_INFO("Render", "Baking reflection probe at (%.1f, %.1f, %.1f) res=%u slot=%u",
      position.x, position.y, position.z, resolution, atlasSlot);
    EnsureResolution(resolution);

    uint32_t srcMipLevels = Detail::MipLevels(resolution);

    VulkanImage cubemap;
    {
      ImageDesc desc {
        .width = resolution,
        .height = resolution,
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
               | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .mipLevels = srcMipLevels,
        .arrayLayers = 6,
        .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
      };
      SamplerDesc sampler {
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = float(srcMipLevels),
      };
      cubemap.Init(*m_Ctx, desc, &sampler);
    }

    VkCommandBuffer cmd = m_Ctx->commandBuffer->BeginSingleTimeCommands();
    TransitionImageLayout(cmd, cubemap.GetImage(),
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);
    m_Ctx->commandBuffer->EndSingleTimeCommands(cmd);

    // Render 6 cube faces using deferred pipeline
    for (uint32_t face = 0; face < 6; face++)
    {
      VulkanImage& litColor = m_OffscreenRenderer.RenderFace(
        frame, position, cubicRes.views[face], resolution);

      cmd = m_Ctx->commandBuffer->BeginSingleTimeCommands();

      TransitionImageLayout(cmd, litColor.GetImage(),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

      // Y-flip blit to undo the Vulkan projection flip from OffscreenRenderer
      VkImageBlit blitRegion {};
      blitRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
      blitRegion.srcOffsets[0] = { 0, int32_t(resolution), 0 };
      blitRegion.srcOffsets[1] = { int32_t(resolution), 0, 1 };
      blitRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, face, 1 };
      blitRegion.dstOffsets[0] = { 0, 0, 0 };
      blitRegion.dstOffsets[1] = { int32_t(resolution), int32_t(resolution), 1 };
      vkCmdBlitImage(cmd,
        litColor.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        cubemap.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &blitRegion, VK_FILTER_NEAREST);

      m_Ctx->commandBuffer->EndSingleTimeCommands(cmd);
    }

    cmd = m_Ctx->commandBuffer->BeginSingleTimeCommands();

    TransitionImageLayout(cmd, cubemap.GetImage(),
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);

    for (uint32_t mip = 1; mip < srcMipLevels; mip++)
    {
      uint32_t srcSize = std::max(1u, resolution >> (mip - 1));
      uint32_t dstSize = std::max(1u, resolution >> mip);

      TransitionImageLayout(cmd, cubemap.GetImage(),
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 6);

      VkImageBlit blit {};
      blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 0, 6 };
      blit.srcOffsets[1] = { int32_t(srcSize), int32_t(srcSize), 1 };
      blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 6 };
      blit.dstOffsets[1] = { int32_t(dstSize), int32_t(dstSize), 1 };

      vkCmdBlitImage(cmd,
        cubemap.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        cubemap.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &blit, VK_FILTER_LINEAR);

      TransitionImageLayout(cmd, cubemap.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 6);
    }

    TransitionImageLayout(cmd, cubemap.GetImage(),
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, srcMipLevels, 0, 6);

    m_Ctx->commandBuffer->EndSingleTimeCommands(cmd);

    // Prefilter only - the irradiance convolution and its readback left with the
    // diffuse role and were a noticeable part of the bake cost.
    VulkanImage prefilterImg = ConvolvePrefilter(*m_Ctx, cubicRes,
      cubemap.GetView(), cubemap.GetSampler(), resolution, PROBE_PREFILTER_SIZE, PROBE_PREFILTER_MIP_LEVELS);

    uint32_t baseLayer = atlasSlot * 6;

    cmd = m_Ctx->commandBuffer->BeginSingleTimeCommands();

    TransitionImageLayout(cmd, prefilterImg.GetImage(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, PROBE_PREFILTER_MIP_LEVELS, 0, 6);
    TransitionImageLayout(cmd, atlas.GetPrefilterImage(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, PROBE_PREFILTER_MIP_LEVELS, baseLayer, 6);

    for (uint32_t mip = 0; mip < PROBE_PREFILTER_MIP_LEVELS; mip++)
    {
      uint32_t mipSize = std::max(1u, PROBE_PREFILTER_SIZE >> mip);
      for (uint32_t f = 0; f < 6; f++)
      {
        VkImageCopy region {};
        region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, f, 1 };
        region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, baseLayer + f, 1 };
        region.extent = { mipSize, mipSize, 1 };
        vkCmdCopyImage(cmd,
          prefilterImg.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          atlas.GetPrefilterImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          1, &region);
      }
    }

    TransitionImageLayout(cmd, atlas.GetPrefilterImage(),
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, PROBE_PREFILTER_MIP_LEVELS, baseLayer, 6);

    m_Ctx->commandBuffer->EndSingleTimeCommands(cmd);

    if (!prefilterSavePath.empty())
    {
      size_t pfTotalSize = CubeMapFile::GetTotalSize(
        PROBE_PREFILTER_SIZE, PROBE_PREFILTER_SIZE, 6, PROBE_PREFILTER_MIP_LEVELS, 8);
      auto pfStaging = VulkanBuffer::CreateReadback(*m_Ctx, pfTotalSize);

      cmd = m_Ctx->commandBuffer->BeginSingleTimeCommands();
      for (uint32_t f = 0; f < 6; f++)
      {
        for (uint32_t mip = 0; mip < PROBE_PREFILTER_MIP_LEVELS; mip++)
        {
          uint32_t mipSize = std::max(1u, PROBE_PREFILTER_SIZE >> mip);
          VkBufferImageCopy region {};
          region.bufferOffset = CubeMapFile::GetMipOffset(
            f, mip, PROBE_PREFILTER_SIZE, PROBE_PREFILTER_SIZE, PROBE_PREFILTER_MIP_LEVELS, 8);
          region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, f, 1 };
          region.imageExtent = { mipSize, mipSize, 1 };
          vkCmdCopyImageToBuffer(cmd, prefilterImg.GetImage(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pfStaging.Get(), 1, &region);
        }
      }
      m_Ctx->commandBuffer->EndSingleTimeCommands(cmd);

      CubeMapFileDesc pfDesc {
        .width = PROBE_PREFILTER_SIZE,
        .height = PROBE_PREFILTER_SIZE,
        .faceCount = 6,
        .mipLevels = PROBE_PREFILTER_MIP_LEVELS,
        .vkFormat = VK_FORMAT_R16G16B16A16_SFLOAT,
        .bytesPerPixel = 8,
        .data = pfStaging.GetMapped(),
        .dataSize = pfTotalSize,
      };
      if (CubeMapFile::Save(prefilterSavePath, pfDesc))
        YA_LOG_INFO("Render", "Saved prefilter: %s", prefilterSavePath.c_str());

      pfStaging.Destroy(*m_Ctx);
    }

    cubemap.Destroy(*m_Ctx);
    prefilterImg.Destroy(*m_Ctx);

    YA_LOG_INFO("Render", "Reflection probe bake complete (slot %u)", atlasSlot);
  }
}

#endif
