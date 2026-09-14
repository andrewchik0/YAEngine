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
  // Every brick of every volume gets one 5x5x5 texel slot of a shared pool: three RGBA16F 3D
  // textures, one per color channel with (L0, L1x, L1y, L1z) per texel, plus an R8 validity
  // texture. SH coefficients are linear, so hardware trilinear filtering inside a slot
  // interpolates them correctly. An R32UI indirection atlas maps each volume's cells to slots.
  // See Core/Shared/IrradianceVolumeData.h for the addressing convention the shader has to match.
  class IrradianceVolumeStorage
  {
  public:

    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    // Slot value for a volume that was not uploaded
    static constexpr uint32_t INVALID_SLOT = UINT32_MAX;

    // Texels the indirection atlas may take, 4 bytes each on the GPU and again while uploading.
    // Differently shaped volumes pad each other out in one 3D texture, so without a cap two thin
    // volumes can ask for gigabytes. Twice the cells of MAX_IRRADIANCE_VOLUMES volumes at the
    // bake's per volume cell cap (BakeLimits::VOLUME_MAX_INDIRECTION_CELLS), for that padding.
    static constexpr uint64_t MAX_INDIRECTION_ATLAS_TEXELS = uint64_t(1) << 26;

    // Replaces everything uploaded before. outSlots gets one entry per input volume, in input
    // order: its index into IrradianceVolumeBuffer::volumes, or INVALID_SLOT when it is
    // inconsistent, does not fit the device's 3D texture limit or MAX_INDIRECTION_ATLAS_TEXELS
    // next to the smaller volumes uploaded before it, or is beyond MAX_IRRADIANCE_VOLUMES.
    // Nothing is released before that is settled and the upload buffers are filled. A failed
    // allocation or submit logs an error and leaves Reset's dummies with every slot INVALID_SLOT.
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
    VkImageView GetIndirectionView() const { return m_Indirection.GetView(); }
    VkSampler GetIndirectionSampler() const { return m_Indirection.GetSampler(); }

    VkBuffer GetBuffer(uint32_t frameIndex) const { return m_UniformBuffers[frameIndex].Get(); }

    uint32_t GetVolumeCount() const { return uint32_t(m_BufferData.volumeCount); }

    // Changes with every Upload and Reset, so a CPU copy of a volume file can tell that the
    // uploaded set was replaced even when its path did not change (a rebake writes in place).
    uint32_t GetGeneration() const { return m_Generation; }

  private:

    void CreateImages(const RenderContext& ctx, const glm::uvec3& poolSize, const glm::uvec3& indirectionSize);
    void DestroyImages(const RenderContext& ctx);

    std::array<VulkanImage, 3> m_Coefficients;
    VulkanImage m_Validity;
    VulkanImage m_Indirection;
    glm::uvec3 m_PoolSize { 1 };
    glm::uvec3 m_IndirectionSize { 1 };
    uint32_t m_Generation = 0;

    IrradianceVolumeBuffer m_BufferData {};
    std::vector<VulkanUniformBuffer> m_UniformBuffers;
  };
}
