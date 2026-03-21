#include "VulkanTexture.h"

#include "RenderContext.h"
#include "VulkanBuffer.h"
#include "VulkanCommandBuffer.h"
#include "ImageBarrier.h"

namespace YAEngine
{
  void VulkanTexture::Load(const RenderContext& ctx, void* inputData, uint32_t width, uint32_t height, uint32_t pixelSize, VkFormat imageFormat, bool repeat)
  {
    VkDeviceSize imageSize = width * height * pixelSize;

    VulkanBuffer staging = VulkanBuffer::CreateStaged(ctx, inputData, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    ImageDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = imageFormat;
    desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.mipLevels = uint32_t(width > height ? glm::log2((float)width) : glm::log2((float)height)) || 1;
    desc.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    SamplerDesc sampler;
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.addressMode = repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.anisotropyEnable = true;
    sampler.maxAnisotropy = 16.0f;

    m_Image.Init(ctx, desc, &sampler);

    VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();
    TransitionImageLayout(cmd, m_Image.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    ctx.commandBuffer->EndSingleTimeCommands(cmd);

    cmd = ctx.commandBuffer->BeginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { width, height, 1 };

    vkCmdCopyBufferToImage(cmd, staging.Get(), m_Image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    ctx.commandBuffer->EndSingleTimeCommands(cmd);

    cmd = ctx.commandBuffer->BeginSingleTimeCommands();
    TransitionImageLayout(cmd, m_Image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ctx.commandBuffer->EndSingleTimeCommands(cmd);

    staging.Destroy(ctx);
  }

  void VulkanTexture::Destroy(const RenderContext& ctx)
  {
    m_Image.Destroy(ctx);
  }
}
