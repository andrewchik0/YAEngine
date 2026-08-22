#ifdef YA_EDITOR

#include "IrradianceVolumeBaker.h"

#include "Render.h"
#include "RenderContext.h"
#include "VulkanCommandBuffer.h"
#include "ImageBarrier.h"
#include "CubemapConvolution.h"
#include "FrameContext.h"
#include "Scene/CollisionQueryService.h"
#include "Utils/Log.h"

namespace YAEngine
{
  void IrradianceVolumeBaker::Init(Render& render, uint32_t resolution)
  {
    m_Render = &render;
    m_Ctx = &render.GetContext();
    m_OffscreenRenderer.Init(render, resolution);
    m_Resolution = resolution;
  }

  void IrradianceVolumeBaker::Destroy()
  {
    if (!m_Ctx) return;

    m_OffscreenRenderer.Destroy();
    m_Ctx = nullptr;
    m_Render = nullptr;
    m_Resolution = 0;
  }

  void IrradianceVolumeBaker::EnsureResolution(uint32_t resolution)
  {
    if (m_Resolution == resolution) return;

    vkDeviceWaitIdle(m_Ctx->device);
    m_OffscreenRenderer.Destroy();
    m_OffscreenRenderer.Init(*m_Render, resolution);
    m_Resolution = resolution;
  }

  IrradianceVolumeBakeResult IrradianceVolumeBaker::Bake(CubicTextureResources& cubicRes,
    FrameContext& frame, Scene& scene, const IrradianceVolumeBakeDesc& desc,
    std::vector<SHL1RGB>& outCoefficients, std::vector<uint8_t>& outValidity)
  {
    IrradianceVolumeBakeResult result;

    if (!desc.layout)
    {
      YA_LOG_ERROR("Render", "Irradiance volume bake called without a grid layout");
      return result;
    }

    const IrradianceGridLayout& layout = *desc.layout;
    uint32_t nodeCount = layout.GetNodeCount();
    result.nodeCount = nodeCount;

    outCoefficients.assign(nodeCount, SHL1RGB {});
    outValidity.assign(nodeCount, 0);

    uint32_t resolution = std::clamp(desc.captureResolution,
      BakeLimits::VOLUME_MIN_CAPTURE_RESOLUTION, BakeLimits::VOLUME_MAX_CAPTURE_RESOLUTION);
    EnsureResolution(resolution);

    // One temporary cubemap reused by every node. No mip chain: SH projection
    // reads mip 0 only, unlike the specular prefilter.
    VulkanImage cubemap;
    {
      ImageDesc imageDesc {
        .width = resolution,
        .height = resolution,
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT
               | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .arrayLayers = 6,
        .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
      };
      SamplerDesc samplerDesc {
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 1.0f,
      };
      cubemap.Init(*m_Ctx, imageDesc, &samplerDesc);
    }

    // Released by scope: ProjectCubemapToSH and EndSingleTimeCommands both
    // throw, and a bake that fails half way would otherwise leak the capture
    // cubemap for the rest of the session.
    struct CubemapScope
    {
      VulkanImage& image;
      const RenderContext& ctx;
      ~CubemapScope() { image.Destroy(ctx); }
    } cubemapScope { cubemap, *m_Ctx };

    // One readback buffer for every node of this volume. ProjectCubemapToSH grows
    // it on the first call and reuses it after that, so a 32k node bake does one
    // VMA allocation here instead of 32k.
    VulkanBuffer readback;
    struct ReadbackScope
    {
      VulkanBuffer& buffer;
      const RenderContext& ctx;
      ~ReadbackScope() { buffer.Destroy(ctx); }
    } readbackScope { readback, *m_Ctx };

    CollisionQueryService collisionQuery(scene);

    // Small probe box around the node center. A quarter of the spacing keeps it
    // well inside its own cell, and the 5 cm floor keeps it meaningful on dense grids.
    glm::vec3 queryHalfExtents = glm::vec3(std::max(layout.spacing * 0.125f, 0.025f));

    uint32_t progressStep = std::max(1u, nodeCount / 20);
    uint32_t processed = 0;

    for (uint32_t z = 0; z < layout.nodeCounts.z; z++)
    {
      for (uint32_t y = 0; y < layout.nodeCounts.y; y++)
      {
        for (uint32_t x = 0; x < layout.nodeCounts.x; x++)
        {
          uint32_t index = layout.GetNodeIndex(x, y, z);
          glm::vec3 worldPosition = layout.GetWorldPosition(x, y, z);

          processed++;
          if (processed % progressStep == 0)
            YA_LOG_INFO("Render", "Volume '%s': node %u/%u", desc.volumeName, processed, nodeCount);

          // Nodes buried in geometry are left invalid and their six renders are
          // skipped entirely - this is where most of the bake time is saved.
          if (!collisionQuery.OverlapAABB(worldPosition - queryHalfExtents,
                worldPosition + queryHalfExtents, desc.colliderMask).empty())
            continue;

          VkCommandBuffer cmd = m_Ctx->commandBuffer->BeginSingleTimeCommands();
          TransitionImageLayout(cmd, cubemap.GetImage(),
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);
          m_Ctx->commandBuffer->EndSingleTimeCommands(cmd);

          for (uint32_t face = 0; face < 6; face++)
          {
            VulkanImage& litColor = m_OffscreenRenderer.RenderFace(
              frame, worldPosition, cubicRes.views[face], resolution);

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
          m_Ctx->commandBuffer->EndSingleTimeCommands(cmd);

          outCoefficients[index] = ProjectCubemapToSH(*m_Ctx, cubemap.GetImage(), resolution,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, &readback);
          outValidity[index] = 1;
          result.anyValid = true;
        }
      }
    }

    for (uint32_t i = 0; i < nodeCount; i++)
    {
      if (outValidity[i] == 0)
        result.rejectedCount++;
    }

    return result;
  }
}

#endif
