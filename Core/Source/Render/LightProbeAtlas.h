#pragma once

#include "VulkanImage.h"
#include "LightProbeData.h"

namespace YAEngine
{
  struct RenderContext;
  class VulkanCubicTexture;

  static constexpr uint32_t PROBE_IRRADIANCE_SIZE = 32;
  static constexpr uint32_t PROBE_PREFILTER_SIZE = 128;
  static constexpr uint32_t PROBE_PREFILTER_MIP_LEVELS = 8; // log2(128) + 1

  class LightProbeAtlas
  {
  public:

    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    // Copy irradiance + prefilter from a VulkanCubicTexture into the given slot
    void UploadSlot(const RenderContext& ctx, uint32_t slotIndex, VulkanCubicTexture& src);

    // Upload irradiance + prefilter from raw pixel data (loaded from .yacm files)
    void UploadSlotFromData(const RenderContext& ctx, uint32_t slotIndex,
      const void* irradianceData, uint32_t irrWidth,
      const void* prefilterData, uint32_t pfWidth, uint32_t pfMipLevels,
      uint32_t bytesPerPixel);

    // Convenience: upload skybox into slot 0
    void UploadSkybox(const RenderContext& ctx, VulkanCubicTexture& skybox);

    VkImage GetIrradianceImage() const { return m_Irradiance.GetImage(); }
    VkImageView GetIrradianceView() const { return m_Irradiance.GetView(); }
    VkSampler GetIrradianceSampler() const { return m_Irradiance.GetSampler(); }

    VkImage GetPrefilterImage() const { return m_Prefilter.GetImage(); }
    VkImageView GetPrefilterView() const { return m_Prefilter.GetView(); }
    VkSampler GetPrefilterSampler() const { return m_Prefilter.GetSampler(); }

    uint32_t GetMaxSlots() const { return m_MaxSlots; }

#ifdef YA_EDITOR
    struct FacePreview
    {
      VkImageView view {};
      VkDescriptorSet descriptor {};
    };

    VkDescriptorSet GetPrefilterFacePreview(const RenderContext& ctx, uint32_t slot, uint32_t face);
    VkDescriptorSet GetIrradianceFacePreview(const RenderContext& ctx, uint32_t slot, uint32_t face);
    void InvalidateSlotPreview(const RenderContext& ctx, uint32_t slot);
    void DestroyPreviews(const RenderContext& ctx);
#endif

  private:

    void CopyCubeFaces(VkCommandBuffer cmd, VkImage srcImage,
      VkImage dstImage, uint32_t slotIndex,
      uint32_t srcSize, uint32_t dstSize,
      uint32_t mipLevels);

    VulkanImage m_Irradiance;
    VulkanImage m_Prefilter;
    uint32_t m_MaxSlots = 0;

#ifdef YA_EDITOR
    struct SlotPreview
    {
      FacePreview prefilter[6] {};
      FacePreview irradiance[6] {};
    };
    std::unordered_map<uint32_t, SlotPreview> m_Previews;

    VkDescriptorSet CreateFacePreview(const RenderContext& ctx, VulkanImage& image,
      uint32_t slot, uint32_t face, FacePreview& out);
#endif
  };
}
