#pragma once

#include "VulkanImage.h"
#include "ReflectionProbeData.h"
#include "Assets/CubeMapFile.h"

namespace YAEngine
{
  struct RenderContext;
  class VulkanCubicTexture;

  static constexpr uint32_t PROBE_IRRADIANCE_SIZE = 64;
  static constexpr uint32_t PROBE_PREFILTER_SIZE = 256;
  static constexpr uint32_t PROBE_PREFILTER_MIP_LEVELS = 9; // log2(256) + 1

  class ReflectionProbeAtlas
  {
  public:

    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    // Upload probe prefilter from a loaded .yacm file. Probes are specular only -
    // their diffuse contribution comes from irradiance volumes now, so no
    // irradiance cubemap is stored for them.
    // Every field of `data` comes straight off disk, so all of them are validated
    // against what the atlas can actually hold. Returns false and leaves the slot
    // untouched when they do not match.
    bool UploadPrefilterFromData(const RenderContext& ctx, uint32_t slotIndex,
      const CubeMapFileData& data);

    // Slot 0 is the only slot that still carries an irradiance cubemap: it is the
    // diffuse fallback for pixels outside every irradiance volume.
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
    };
    std::unordered_map<uint32_t, SlotPreview> m_Previews;

    VkDescriptorSet CreateFacePreview(const RenderContext& ctx, VulkanImage& image,
      uint32_t slot, uint32_t face, FacePreview& out);
#endif
  };
}
