#pragma once

#include "VulkanImage.h"

namespace YAEngine
{
  struct RenderContext;
  struct CubicTextureResources;

  // Convolve a source cubemap into a diffuse irradiance cubemap.
  // Returns a VulkanImage (cubemap, 6 layers, R16G16B16A16_SFLOAT, SHADER_READ_ONLY layout).
  // Caller owns the returned image and must call Destroy() on it.
  VulkanImage ConvolveIrradiance(const RenderContext& ctx, CubicTextureResources& cubicRes,
    VkImageView srcView, VkSampler srcSampler, uint32_t outputSize);

  // Convolve a source cubemap into a specular prefilter cubemap with mip chain.
  // Returns a VulkanImage (cubemap, 6 layers, R16G16B16A16_SFLOAT, SHADER_READ_ONLY layout).
  // Caller owns the returned image and must call Destroy() on it.
  VulkanImage ConvolvePrefilter(const RenderContext& ctx, CubicTextureResources& cubicRes,
    VkImageView srcView, VkSampler srcSampler, uint32_t outputSize, uint32_t mipLevels);
}
