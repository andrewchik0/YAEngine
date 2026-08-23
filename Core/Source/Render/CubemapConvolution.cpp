#include "CubemapConvolution.h"

#include <glm/gtc/packing.hpp>

#include "RenderContext.h"
#include "VulkanCommandBuffer.h"
#include "VulkanCubicTexture.h"
#include "VulkanBuffer.h"
#include "ImageBarrier.h"
#include "Utils/Log.h"

namespace YAEngine
{
  VulkanImage ConvolveIrradiance(const RenderContext& ctx, CubicTextureResources& cubicRes,
    VkImageView srcView, VkSampler srcSampler, uint32_t outputSize)
  {
    VulkanImage result;
    {
      ImageDesc desc {
        .width = outputSize,
        .height = outputSize,
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .arrayLayers = 6,
        .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
      };
      SamplerDesc sampler {
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .anisotropyEnable = true,
        .maxAnisotropy = 16.0f,
        .maxLod = 1.0f,
      };
      result.Init(ctx, desc, &sampler);
    }

    VkImageView faceViews[6] {};
    VkFramebuffer framebuffers[6] {};

    for (uint32_t face = 0; face < 6; face++)
    {
      VkImageViewCreateInfo vi {};
      vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      vi.image = result.GetImage();
      vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
      vi.format = VK_FORMAT_R16G16B16A16_SFLOAT;
      vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, face, 1 };
      if (vkCreateImageView(ctx.device, &vi, nullptr, &faceViews[face]) != VK_SUCCESS)
      {
        YA_LOG_ERROR("Vulkan", "Failed to create irradiance face view %u", face);
        throw std::runtime_error("Failed to create irradiance face view");
      }

      VkFramebufferCreateInfo fb {};
      fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fb.renderPass = cubicRes.irradianceRenderPass;
      fb.attachmentCount = 1;
      fb.pAttachments = &faceViews[face];
      fb.width = outputSize;
      fb.height = outputSize;
      fb.layers = 1;
      if (vkCreateFramebuffer(ctx.device, &fb, nullptr, &framebuffers[face]) != VK_SUCCESS)
      {
        YA_LOG_ERROR("Vulkan", "Failed to create irradiance framebuffer %u", face);
        throw std::runtime_error("Failed to create irradiance framebuffer");
      }
    }

    auto cmd = ctx.commandBuffer->BeginSingleTimeCommands();
    TransitionImageLayout(cmd, result.GetImage(),
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);
    ctx.commandBuffer->EndSingleTimeCommands(cmd);

    // Temporary descriptor pool so the set is freed when we destroy it
    VkDescriptorPool tempPool;
    {
      VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
      VkDescriptorPoolCreateInfo poolCI {};
      poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
      poolCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
      poolCI.maxSets = 1;
      poolCI.poolSizeCount = 1;
      poolCI.pPoolSizes = &poolSize;
      if (vkCreateDescriptorPool(ctx.device, &poolCI, nullptr, &tempPool) != VK_SUCCESS)
      {
        YA_LOG_ERROR("Vulkan", "Failed to create irradiance descriptor pool");
        throw std::runtime_error("Failed to create irradiance descriptor pool");
      }
    }

    VkDescriptorSet descriptorSet;
    {
      VkDescriptorSetAllocateInfo allocInfo {};
      allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      allocInfo.descriptorPool = tempPool;
      allocInfo.descriptorSetCount = 1;
      allocInfo.pSetLayouts = &cubicRes.irradianceDescriptorSetLayout;
      if (vkAllocateDescriptorSets(ctx.device, &allocInfo, &descriptorSet) != VK_SUCCESS)
      {
        YA_LOG_ERROR("Vulkan", "Failed to allocate irradiance descriptor set");
        throw std::runtime_error("Failed to allocate irradiance descriptor set");
      }

      VkDescriptorImageInfo imgInfo {};
      imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      imgInfo.imageView = srcView;
      imgInfo.sampler = srcSampler;

      VkWriteDescriptorSet write {};
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet = descriptorSet;
      write.dstBinding = 0;
      write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      write.descriptorCount = 1;
      write.pImageInfo = &imgInfo;
      vkUpdateDescriptorSets(ctx.device, 1, &write, 0, nullptr);
    }

    for (uint32_t face = 0; face < 6; face++)
    {
      cmd = ctx.commandBuffer->BeginSingleTimeCommands();

      VkClearValue clear {};
      clear.color = { 0, 0, 0, 1 };

      VkRenderPassBeginInfo rp {};
      rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      rp.renderPass = cubicRes.irradianceRenderPass;
      rp.framebuffer = framebuffers[face];
      rp.renderArea.extent = { outputSize, outputSize };
      rp.clearValueCount = 1;
      rp.pClearValues = &clear;

      vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cubicRes.irradiancePipeline);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        cubicRes.irradiancePipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

      VkViewport vp { 0, 0, float(outputSize), float(outputSize), 0, 1 };
      VkRect2D sc { {0, 0}, {outputSize, outputSize} };
      vkCmdSetViewport(cmd, 0, 1, &vp);
      vkCmdSetScissor(cmd, 0, 1, &sc);

      glm::mat4 vpMat = cubicRes.projection * cubicRes.views[face];
      vkCmdPushConstants(cmd, cubicRes.irradiancePipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &vpMat);

      VulkanCubicTexture::DrawCube(cmd, cubicRes);
      vkCmdEndRenderPass(cmd);
      ctx.commandBuffer->EndSingleTimeCommands(cmd);
    }

    cmd = ctx.commandBuffer->BeginSingleTimeCommands();
    TransitionImageLayout(cmd, result.GetImage(),
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);
    ctx.commandBuffer->EndSingleTimeCommands(cmd);

    for (uint32_t face = 0; face < 6; face++)
    {
      vkDestroyFramebuffer(ctx.device, framebuffers[face], nullptr);
      vkDestroyImageView(ctx.device, faceViews[face], nullptr);
    }

    vkDestroyDescriptorPool(ctx.device, tempPool, nullptr);

    return result;
  }

  VulkanImage ConvolvePrefilter(const RenderContext& ctx, CubicTextureResources& cubicRes,
    VkImageView srcView, VkSampler srcSampler, uint32_t srcResolution,
    uint32_t outputSize, uint32_t mipLevels)
  {
    VulkanImage result;
    {
      ImageDesc desc {
        .width = outputSize,
        .height = outputSize,
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .mipLevels = mipLevels,
        .arrayLayers = 6,
        .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
      };
      SamplerDesc sampler {
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .anisotropyEnable = true,
        .maxAnisotropy = 16.0f,
        .maxLod = float(mipLevels),
      };
      result.Init(ctx, desc, &sampler);
    }

    uint32_t totalFaceViews = 6 * mipLevels;
    std::vector<VkImageView> faceViews(totalFaceViews);
    std::vector<VkFramebuffer> framebuffers(totalFaceViews);

    for (uint32_t mip = 0; mip < mipLevels; mip++)
    {
      uint32_t mipSize = std::max(1u, outputSize >> mip);
      for (uint32_t face = 0; face < 6; face++)
      {
        uint32_t idx = mip * 6 + face;

        VkImageViewCreateInfo vi {};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = result.GetImage();
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, face, 1 };
        if (vkCreateImageView(ctx.device, &vi, nullptr, &faceViews[idx]) != VK_SUCCESS)
        {
          YA_LOG_ERROR("Vulkan", "Failed to create prefilter face view mip %u face %u", mip, face);
          throw std::runtime_error("Failed to create prefilter face view");
        }

        VkFramebufferCreateInfo fb {};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = cubicRes.prefilterRenderPass;
        fb.attachmentCount = 1;
        fb.pAttachments = &faceViews[idx];
        fb.width = mipSize;
        fb.height = mipSize;
        fb.layers = 1;
        if (vkCreateFramebuffer(ctx.device, &fb, nullptr, &framebuffers[idx]) != VK_SUCCESS)
        {
          YA_LOG_ERROR("Vulkan", "Failed to create prefilter framebuffer mip %u face %u", mip, face);
          throw std::runtime_error("Failed to create prefilter framebuffer");
        }
      }
    }

    auto cmd = ctx.commandBuffer->BeginSingleTimeCommands();
    TransitionImageLayout(cmd, result.GetImage(),
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 6);
    ctx.commandBuffer->EndSingleTimeCommands(cmd);

    // Temporary descriptor pool so the set is freed when we destroy it
    VkDescriptorPool tempPool;
    {
      VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
      VkDescriptorPoolCreateInfo poolCI {};
      poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
      poolCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
      poolCI.maxSets = 1;
      poolCI.poolSizeCount = 1;
      poolCI.pPoolSizes = &poolSize;
      if (vkCreateDescriptorPool(ctx.device, &poolCI, nullptr, &tempPool) != VK_SUCCESS)
      {
        YA_LOG_ERROR("Vulkan", "Failed to create prefilter descriptor pool");
        throw std::runtime_error("Failed to create prefilter descriptor pool");
      }
    }

    VkDescriptorSet descriptorSet;
    {
      VkDescriptorSetAllocateInfo allocInfo {};
      allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      allocInfo.descriptorPool = tempPool;
      allocInfo.descriptorSetCount = 1;
      allocInfo.pSetLayouts = &cubicRes.prefilterDescriptorSetLayout;
      if (vkAllocateDescriptorSets(ctx.device, &allocInfo, &descriptorSet) != VK_SUCCESS)
      {
        YA_LOG_ERROR("Vulkan", "Failed to allocate prefilter descriptor set");
        throw std::runtime_error("Failed to allocate prefilter descriptor set");
      }

      VkDescriptorImageInfo imgInfo {};
      imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      imgInfo.imageView = srcView;
      imgInfo.sampler = srcSampler;

      VkWriteDescriptorSet write {};
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet = descriptorSet;
      write.dstBinding = 0;
      write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      write.descriptorCount = 1;
      write.pImageInfo = &imgInfo;
      vkUpdateDescriptorSets(ctx.device, 1, &write, 0, nullptr);
    }

    for (uint32_t mip = 0; mip < mipLevels; mip++)
    {
      uint32_t mipSize = std::max(1u, outputSize >> mip);
      float roughness = float(mip) / float(mipLevels - 1);

      for (uint32_t face = 0; face < 6; face++)
      {
        cmd = ctx.commandBuffer->BeginSingleTimeCommands();

        VkClearValue clear {};
        clear.color = { 0, 0, 0, 1 };

        VkRenderPassBeginInfo rp {};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = cubicRes.prefilterRenderPass;
        rp.framebuffer = framebuffers[mip * 6 + face];
        rp.renderArea.extent = { mipSize, mipSize };
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;

        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cubicRes.prefilterPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
          cubicRes.prefilterPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

        VkViewport vp { 0, 0, float(mipSize), float(mipSize), 0, 1 };
        VkRect2D sc { {0, 0}, {mipSize, mipSize} };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);

        struct { glm::mat4 vp; float roughness; float srcResolution; } pc;
        pc.vp = cubicRes.projection * cubicRes.views[face];
        pc.roughness = roughness;
        pc.srcResolution = float(srcResolution);
        vkCmdPushConstants(cmd, cubicRes.prefilterPipelineLayout,
          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
          0, sizeof(pc), &pc);

        VulkanCubicTexture::DrawCube(cmd, cubicRes);
        vkCmdEndRenderPass(cmd);
        ctx.commandBuffer->EndSingleTimeCommands(cmd);
      }
    }

    cmd = ctx.commandBuffer->BeginSingleTimeCommands();
    TransitionImageLayout(cmd, result.GetImage(),
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 6);
    ctx.commandBuffer->EndSingleTimeCommands(cmd);

    for (uint32_t i = 0; i < totalFaceViews; i++)
    {
      vkDestroyFramebuffer(ctx.device, framebuffers[i], nullptr);
      vkDestroyImageView(ctx.device, faceViews[i], nullptr);
    }

    vkDestroyDescriptorPool(ctx.device, tempPool, nullptr);

    return result;
  }

  SHL1RGB ProjectCubemapToSH(const RenderContext& ctx, VkImage srcImage, uint32_t resolution,
    VkImageLayout currentLayout, uint32_t mipLevel, VulkanBuffer* reusableStaging)
  {
    if (resolution == 0)
    {
      YA_LOG_ERROR("Render", "ProjectCubemapToSH called with zero resolution");
      return SHL1RGB {};
    }

    // Four half-float channels per texel
    constexpr size_t BYTES_PER_TEXEL = 8;
    size_t faceTexels = size_t(resolution) * size_t(resolution);
    size_t faceBytes = faceTexels * BYTES_PER_TEXEL;

    const VkDeviceSize readbackSize = faceBytes * 6;

    VulkanBuffer localStaging;
    if (reusableStaging != nullptr && reusableStaging->GetSize() < readbackSize)
    {
      reusableStaging->Destroy(ctx);
      *reusableStaging = VulkanBuffer::CreateReadback(ctx, readbackSize);
    }
    else if (reusableStaging == nullptr)
    {
      localStaging = VulkanBuffer::CreateReadback(ctx, readbackSize);
    }

    VulkanBuffer& staging = reusableStaging != nullptr ? *reusableStaging : localStaging;

    bool needsTransition = currentLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
      && currentLayout != VK_IMAGE_LAYOUT_UNDEFINED;

    VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();

    // UNDEFINED is documented as "already usable as a transfer source, do not
    // restore", and s_BarrierTable has no UNDEFINED -> TRANSFER_SRC entry, so
    // transitioning it would emit a barrier with zeroed stage masks.
    if (needsTransition)
      TransitionImageLayout(cmd, srcImage, currentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, mipLevel, 1, 0, 6);

    for (uint32_t face = 0; face < 6; face++)
    {
      VkBufferImageCopy region {};
      region.bufferOffset = faceBytes * face;
      region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mipLevel, face, 1 };
      region.imageExtent = { resolution, resolution, 1 };
      vkCmdCopyImageToBuffer(cmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        staging.Get(), 1, &region);
    }

    if (needsTransition)
      TransitionImageLayout(cmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, currentLayout,
        VK_IMAGE_ASPECT_COLOR_BIT, mipLevel, 1, 0, 6);

    ctx.commandBuffer->EndSingleTimeCommands(cmd);

    const uint16_t* texels = static_cast<const uint16_t*>(staging.GetMapped());
    if (texels == nullptr)
    {
      YA_LOG_ERROR("Render", "ProjectCubemapToSH got an unmapped readback buffer");
      localStaging.Destroy(ctx);
      return SHL1RGB {};
    }

    // The solid angle is face independent, and it costs four atan2 per texel. One
    // volume bake calls this once per node, so recomputing it six times over would
    // dominate the CPU half of the bake.
    std::vector<float> solidAngles(faceTexels);
    for (uint32_t y = 0; y < resolution; y++)
    {
      for (uint32_t x = 0; x < resolution; x++)
        solidAngles[size_t(y) * resolution + x] = CubeFaceTexelSolidAngle(x, y, resolution);
    }

    SHL1Accumulator accumulator;
    for (uint32_t face = 0; face < 6; face++)
    {
      for (uint32_t y = 0; y < resolution; y++)
      {
        for (uint32_t x = 0; x < resolution; x++)
        {
          const uint16_t* texel = texels + (face * faceTexels + size_t(y) * resolution + x) * 4;
          glm::vec3 color(
            glm::unpackHalf1x16(texel[0]),
            glm::unpackHalf1x16(texel[1]),
            glm::unpackHalf1x16(texel[2]));

          // A single Inf or NaN texel - representable in RGBA16F - would otherwise
          // poison l0 and spread over everything the volume covers.
          if (!std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z))
            continue;

          accumulator.AddSample(color,
            CubeFaceTexelDirection(face, x, y, resolution),
            solidAngles[size_t(y) * resolution + x]);
        }
      }
    }

    // Only the local one: a reused buffer belongs to the caller.
    localStaging.Destroy(ctx);

    return accumulator.Finalize();
  }

  float ComputeCubemapBackfaceRatio(const RenderContext& ctx, VkImage srcImage,
    uint32_t resolution, VkImageLayout currentLayout, VulkanBuffer* reusableStaging)
  {
    if (resolution == 0)
    {
      YA_LOG_ERROR("Render", "ComputeCubemapBackfaceRatio called with zero resolution");
      return 0.0f;
    }

    size_t faceTexels = size_t(resolution) * size_t(resolution);
    const VkDeviceSize readbackSize = faceTexels * 6;

    VulkanBuffer localStaging;
    if (reusableStaging != nullptr && reusableStaging->GetSize() < readbackSize)
    {
      reusableStaging->Destroy(ctx);
      *reusableStaging = VulkanBuffer::CreateReadback(ctx, readbackSize);
    }
    else if (reusableStaging == nullptr)
    {
      localStaging = VulkanBuffer::CreateReadback(ctx, readbackSize);
    }

    VulkanBuffer& staging = reusableStaging != nullptr ? *reusableStaging : localStaging;

    bool needsTransition = currentLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
      && currentLayout != VK_IMAGE_LAYOUT_UNDEFINED;

    VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();

    if (needsTransition)
      TransitionImageLayout(cmd, srcImage, currentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);

    for (uint32_t face = 0; face < 6; face++)
    {
      VkBufferImageCopy region {};
      region.bufferOffset = faceTexels * face;
      region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, face, 1 };
      region.imageExtent = { resolution, resolution, 1 };
      vkCmdCopyImageToBuffer(cmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        staging.Get(), 1, &region);
    }

    if (needsTransition)
      TransitionImageLayout(cmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, currentLayout,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);

    ctx.commandBuffer->EndSingleTimeCommands(cmd);

    const uint8_t* texels = static_cast<const uint8_t*>(staging.GetMapped());
    if (texels == nullptr)
    {
      YA_LOG_ERROR("Render", "ComputeCubemapBackfaceRatio got an unmapped readback buffer");
      localStaging.Destroy(ctx);
      return 0.0f;
    }

    // Same reason as in ProjectCubemapToSH: four atan2 per texel, recomputed six
    // times over would dominate the CPU half of the bake.
    std::vector<float> solidAngles(faceTexels);
    float faceSolidAngle = 0.0f;
    for (uint32_t y = 0; y < resolution; y++)
    {
      for (uint32_t x = 0; x < resolution; x++)
      {
        float sa = CubeFaceTexelSolidAngle(x, y, resolution);
        solidAngles[size_t(y) * resolution + x] = sa;
        faceSolidAngle += sa;
      }
    }

    // The mask is a vertically flipped view of the face, because the capture keeps
    // the Vulkan projection flip. A face's texel solid angles are symmetric about
    // its center on both axes, so the flip cannot change the sum and the readback
    // needs no unflipping blit.
    float backfaceSolidAngle = 0.0f;
    for (uint32_t face = 0; face < 6; face++)
    {
      const uint8_t* faceTexelData = texels + face * faceTexels;
      for (size_t i = 0; i < faceTexels; i++)
      {
        if (faceTexelData[i] >= 128)
          backfaceSolidAngle += solidAngles[i];
      }
    }

    localStaging.Destroy(ctx);

    float totalSolidAngle = faceSolidAngle * 6.0f;
    if (totalSolidAngle <= 0.0f)
      return 0.0f;

    return backfaceSolidAngle / totalSolidAngle;
  }
}
