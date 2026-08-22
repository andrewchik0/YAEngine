#pragma once

#include "Pch.h"
#include "VulkanImage.h"

namespace YAEngine
{
  struct RenderContext;

  // BC5 has no blue channel, so a normal map in this format carries X and Y only
  // and the shader has to rebuild Z. Shared by VulkanMaterial and
  // VulkanTerrainMaterial, which both set a "two channel" bit for it.
  inline bool IsTwoChannelNormal(VkFormat format)
  {
    return format == VK_FORMAT_BC5_UNORM_BLOCK;
  }

  class VulkanTexture
  {
  public:

    void Load(const RenderContext& ctx, void* data, uint32_t width, uint32_t height, uint32_t pixelSize, VkFormat format, bool repeat = true, bool preserveAlphaCoverage = false);
    // False when the data cannot be turned into an image. The texture is left
    // invalid in that case, so callers must not hand it out as a live handle.
    bool LoadFromCpuData(const RenderContext& ctx, const struct CpuTextureData& cpuData, VkFormat format);
    void Destroy(const RenderContext& ctx);

    VkImageView GetView() const { return m_Image.GetView(); }
    VkSampler GetSampler() const { return m_Image.GetSampler(); }
    VkImage GetImage() const { return m_Image.GetImage(); }
    VkFormat GetFormat() const { return m_Image.GetFormat(); }

    bool IsValid() const { return m_Image.IsValid(); }

  private:

    VulkanImage m_Image;
  };
}
