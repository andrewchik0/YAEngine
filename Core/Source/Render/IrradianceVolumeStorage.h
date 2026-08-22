#pragma once

#include "VulkanImage.h"
#include "VulkanUniformBuffer.h"
#include "IrradianceVolumeData.h"
#include "Assets/IrradianceVolumeFile.h"

namespace YAEngine
{
  struct RenderContext;

  // GPU side of the baked irradiance volumes.
  //
  // Coefficients live in three RGBA16F 3D textures, one per color channel, each
  // texel holding (L0, L1x, L1y, L1z) of that channel. SH coefficients are linear,
  // so hardware trilinear filtering interpolates them correctly and one fetch per
  // channel replaces a manual 8-tap gather.
  //
  // Vulkan has no 3D texture arrays, so all volumes share one texture and are
  // packed along X. See Core/Shared/IrradianceVolumeData.h for the addressing
  // convention the shader has to match.
  class IrradianceVolumeStorage
  {
  public:

    // One unused texel column between neighbouring sub-boxes. The sampling math
    // already stops at texel centers, this is the belt on top of the braces.
    static constexpr uint32_t VOLUME_GAP_TEXELS = 1;

    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    // Slot value for a volume that did not fit into the atlas
    static constexpr uint32_t INVALID_SLOT = UINT32_MAX;

    // Rebuilds the atlas for the given volumes and uploads them. The volumes are
    // sorted by ascending box volume, so a nested interior volume ends up in a
    // lower slot and wins the first-hit test in the shader.
    // outSlots is filled with the atlas slot of every input volume, in input order,
    // or INVALID_SLOT when the volume did not fit.
    // The image views change, so the caller MUST rewrite the IBL descriptor set.
    void Upload(const RenderContext& ctx, const std::vector<IrradianceVolumeFileData>& volumes,
      std::vector<uint32_t>& outSlots);

    // Back to the 1x1x1 dummies and volumeCount = 0 - the shader then takes
    // skybox irradiance everywhere. Descriptors must be rewritten afterwards.
    void Reset(const RenderContext& ctx);

    void SetUp(uint32_t frameIndex, const IrradianceVolumeBuffer& data);

    // Description of everything currently uploaded, ready to be pushed per frame.
    const IrradianceVolumeBuffer& GetBufferData() const { return m_BufferData; }

    VkImageView GetCoefficientView(uint32_t channel) const { return m_Coefficients[channel].GetView(); }
    VkSampler GetCoefficientSampler(uint32_t channel) const { return m_Coefficients[channel].GetSampler(); }
    VkImageView GetValidityView() const { return m_Validity.GetView(); }
    VkSampler GetValiditySampler() const { return m_Validity.GetSampler(); }

    VkBuffer GetBuffer(uint32_t frameIndex) const { return m_UniformBuffers[frameIndex].Get(); }

    glm::uvec3 GetAtlasSize() const { return m_AtlasSize; }
    uint32_t GetVolumeCount() const { return uint32_t(m_BufferData.volumeCount); }

  private:

    void CreateImages(const RenderContext& ctx, const glm::uvec3& size);
    void DestroyImages(const RenderContext& ctx);

    std::array<VulkanImage, 3> m_Coefficients;
    VulkanImage m_Validity;
    glm::uvec3 m_AtlasSize { 1 };

    IrradianceVolumeBuffer m_BufferData {};
    std::vector<VulkanUniformBuffer> m_UniformBuffers;
  };
}
