#include "CubemapConvolution.h"

#include "RenderContext.h"
#include "VulkanCommandBuffer.h"
#include "VulkanCubicTexture.h"
#include "ImageBarrier.h"

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
      vkCreateImageView(ctx.device, &vi, nullptr, &faceViews[face]);

      VkFramebufferCreateInfo fb {};
      fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fb.renderPass = cubicRes.irradianceRenderPass;
      fb.attachmentCount = 1;
      fb.pAttachments = &faceViews[face];
      fb.width = outputSize;
      fb.height = outputSize;
      fb.layers = 1;
      vkCreateFramebuffer(ctx.device, &fb, nullptr, &framebuffers[face]);
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
      vkCreateDescriptorPool(ctx.device, &poolCI, nullptr, &tempPool);
    }

    VkDescriptorSet descriptorSet;
    {
      VkDescriptorSetAllocateInfo allocInfo {};
      allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      allocInfo.descriptorPool = tempPool;
      allocInfo.descriptorSetCount = 1;
      allocInfo.pSetLayouts = &cubicRes.irradianceDescriptorSetLayout;
      vkAllocateDescriptorSets(ctx.device, &allocInfo, &descriptorSet);

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
        vkCreateImageView(ctx.device, &vi, nullptr, &faceViews[idx]);

        VkFramebufferCreateInfo fb {};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = cubicRes.prefilterRenderPass;
        fb.attachmentCount = 1;
        fb.pAttachments = &faceViews[idx];
        fb.width = mipSize;
        fb.height = mipSize;
        fb.layers = 1;
        vkCreateFramebuffer(ctx.device, &fb, nullptr, &framebuffers[idx]);
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
      vkCreateDescriptorPool(ctx.device, &poolCI, nullptr, &tempPool);
    }

    VkDescriptorSet descriptorSet;
    {
      VkDescriptorSetAllocateInfo allocInfo {};
      allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      allocInfo.descriptorPool = tempPool;
      allocInfo.descriptorSetCount = 1;
      allocInfo.pSetLayouts = &cubicRes.prefilterDescriptorSetLayout;
      vkAllocateDescriptorSets(ctx.device, &allocInfo, &descriptorSet);

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
}
