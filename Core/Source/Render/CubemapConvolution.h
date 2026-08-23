#pragma once

#include "VulkanImage.h"
#include "VulkanBuffer.h"
#include "Utils/SphericalHarmonics.h"

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
    VkImageView srcView, VkSampler srcSampler, uint32_t srcResolution,
    uint32_t outputSize, uint32_t mipLevels);

  // Project one mip of a R16G16B16A16_SFLOAT cubemap into SH L1 on the CPU.
  // Synchronous readback of all six faces - only ever called during a bake, never
  // inside a frame. resolution is the face size OF THAT MIP. currentLayout must be
  // the layout the image is actually in; it is restored before returning
  // (UNDEFINED means "do not restore").
  // reusableStaging lets a caller that runs this per grid node keep one readback
  // buffer instead of allocating and freeing one every time; it is grown when it
  // is too small and left alone otherwise. Pass nullptr for a one-off call.
  SHL1RGB ProjectCubemapToSH(const RenderContext& ctx, VkImage srcImage, uint32_t resolution,
    VkImageLayout currentLayout, uint32_t mipLevel = 0,
    VulkanBuffer* reusableStaging = nullptr);

  // Fraction of the sphere a BackfaceRatioSampler cube says is covered by geometry
  // turned inside out towards the capture point - one for a fully enclosed point,
  // zero for one in open space. Weighted by texel solid angle, so the corners of a
  // cube face do not count for more than its center.
  //
  // srcImage must be a six layer R8_UNORM cube. Readback and layout handling follow
  // ProjectCubemapToSH exactly, including reusableStaging, but the two cannot share
  // one buffer: the formats and therefore the sizes differ.
  float ComputeCubemapBackfaceRatio(const RenderContext& ctx, VkImage srcImage,
    uint32_t resolution, VkImageLayout currentLayout,
    VulkanBuffer* reusableStaging = nullptr);
}
