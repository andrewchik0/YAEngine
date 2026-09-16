#include "RayTracingMaterialTable.h"

#include <glm/gtc/packing.hpp>

#include "BindlessTextureRegistry.h"
#include "RenderContext.h"
#include "VulkanTexture.h"
#include "Assets/MaterialManager.h"
#include "Assets/TextureManager.h"
#include "Utils/Log.h"

namespace YAEngine
{
  namespace
  {
    constexpr VkBufferUsageFlags RECORD_BUFFER_USAGE = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    // A texture the material names but that no longer resolves - destroyed since the
    // material was authored, or never loaded - lands on the white fallback, and its
    // presence bit is left clear so nothing samples it anyway.
    uint32_t ResolveTexture(TextureManager& textures, TextureHandle handle,
      uint32_t maskBit, uint32_t& textureMask, const VulkanTexture** outTexture = nullptr)
    {
      if (!textures.Has(handle))
        return BindlessTextureRegistry::FALLBACK_INDEX;

      const VulkanTexture& texture = textures.GetVulkanTexture(handle);
      if (outTexture != nullptr)
        *outTexture = &texture;

      textureMask |= maskBit;
      return textures.GetBindlessIndex(handle);
    }

    // One step of SnapshotDigest's fold (SceneSnapshot.h), kept local for the reason
    // Render.Draw.cpp gives for its own copy.
    uint64_t FoldWord(uint64_t digest, uint64_t word)
    {
      word *= 0xff51afd7ed558ccdull;
      word ^= word >> 33;
      digest = (digest ^ word) * 1099511628211ull;
      return digest ^ (digest >> 29);
    }

    uint64_t FloatPair(float low, float high)
    {
      return uint64_t(glm::floatBitsToUint(low)) | (uint64_t(glm::floatBitsToUint(high)) << 32);
    }

    // A black transmittance channel absorbs as much as a float can carry, rather than taking the
    // logarithm of zero.
    constexpr float MIN_TRANSMITTANCE = 1e-4f;
  }

  void RayTracingMaterialTable::Init(const RenderContext& ctx)
  {
    uint32_t slotCount = ctx.maxFramesInFlight;
#ifdef YA_EDITOR
    m_BakeSlot = ctx.maxFramesInFlight;
    slotCount++;
#endif

    if (!ctx.raytracingSupported)
      return;

    m_Slots.resize(slotCount);
    for (FrameSlot& slot : m_Slots)
      EnsureCapacity(ctx, slot, INITIAL_CAPACITY);
  }

  void RayTracingMaterialTable::Destroy(const RenderContext& ctx)
  {
    for (FrameSlot& slot : m_Slots)
      slot.records.Destroy(ctx);

    m_Slots.clear();
    m_Staging.clear();
  }

  uint32_t RayTracingMaterialTable::EnsureCapacity(const RenderContext& ctx, FrameSlot& slot,
    uint32_t required)
  {
    uint32_t capacity = uint32_t(slot.records.GetSize() / sizeof(RayTracingMaterialRecord));

    uint32_t target = std::max(capacity, INITIAL_CAPACITY);
    while (target < required && target < MAX_CAPACITY)
      target = std::min(target * 2, MAX_CAPACITY);

    if (target > capacity)
    {
      // Nothing in flight still reads the old allocation - a frame slot's fence has been
      // waited on, and the bake slot is only used on single-time command buffers, which wait
      // for completion - so it can go now rather than onto a deferred destroy queue.
      slot.records.Destroy(ctx);
      slot.records = VulkanBuffer::CreateMapped(ctx,
        VkDeviceSize(target) * sizeof(RayTracingMaterialRecord), RECORD_BUFFER_USAGE);
      capacity = target;
    }

    return capacity;
  }

  void RayTracingMaterialTable::Update(const RenderContext& ctx, uint32_t frameIndex,
    MaterialManager& materials, TextureManager& textures)
  {
    if (frameIndex >= m_Slots.size())
      return;

    FrameSlot& slot = m_Slots[frameIndex];
    slot.recordCount = 0;
    slot.pathTraceDigest = 0;

    // The table is addressed by slot index, not by a compacted material order, so its
    // length is the highest live slot plus one and not the live material count.
    uint32_t required = 0;
    materials.ForEachWithHandle([&](MaterialHandle handle, Material&) {
      required = std::max(required, handle.index + 1);
    });

    if (required == 0)
      return;

    uint32_t capacity = EnsureCapacity(ctx, slot, required);
    if (capacity == 0)
      return;

#ifdef YA_EDITOR
    const bool frameSlot = frameIndex != m_BakeSlot;
#else
    const bool frameSlot = true;
#endif

    if (required > capacity && (!frameSlot || !b_CapacityWarned))
    {
      b_CapacityWarned = b_CapacityWarned || frameSlot;
      YA_LOG_WARN("Render",
        "Ray tracing material table%s capped at %u records while %u was requested, the excess materials are dropped",
        frameSlot ? "" : " bake slot", capacity, required);
    }

    const uint32_t count = std::min(required, capacity);

    // Zeroed first so the holes a slot map leaves between live materials hold a defined
    // record. Nothing indexes one - an instance is only written for a material that
    // resolved this same frame - but a garbage index must not read garbage. memset rather
    // than value initialization: whether a defaulted GLM vector zeroes itself is a GLM
    // build setting, and the staging vector is reused across frames either way.
    m_Staging.resize(count);
    std::memset(m_Staging.data(), 0, size_t(count) * sizeof(RayTracingMaterialRecord));
    m_StagingTransmission.assign(count, TransmissionMode::None);

    materials.ForEachWithHandle([&](MaterialHandle handle, Material& material) {
      if (handle.index >= count)
        return;

      RayTracingMaterialRecord& record = m_Staging[handle.index];

      uint32_t textureMask = 0;
      const VulkanTexture* normalTexture = nullptr;

      record.baseColorIndex = ResolveTexture(textures, material.baseColorTexture,
        RT_MATERIAL_BASE_COLOR, textureMask);
      record.metallicIndex = ResolveTexture(textures, material.metallicTexture,
        RT_MATERIAL_METALLIC, textureMask);
      record.roughnessIndex = ResolveTexture(textures, material.roughnessTexture,
        RT_MATERIAL_ROUGHNESS, textureMask);
      record.specularIndex = ResolveTexture(textures, material.specularTexture,
        RT_MATERIAL_SPECULAR, textureMask);
      record.emissiveIndex = ResolveTexture(textures, material.emissiveTexture,
        RT_MATERIAL_EMISSIVE_MAP, textureMask);
      record.normalIndex = ResolveTexture(textures, material.normalTexture,
        RT_MATERIAL_NORMAL, textureMask, &normalTexture);

      if (normalTexture != nullptr && IsTwoChannelNormal(normalTexture->GetFormat()))
        textureMask |= RT_MATERIAL_TWO_CHANNEL_NORMAL;
      if (material.combinedTextures)
        textureMask |= RT_MATERIAL_COMBINED;
      if (material.emissive)
        textureMask |= RT_MATERIAL_EMISSIVE_SHADING;

      record.albedo = material.albedo;
      record.roughness = material.roughness;
      // Folded together exactly as VulkanMaterial::Bind does: glTF keeps the factor and its
      // strength apart so both survive a round trip, no shader ever wants them separately.
      record.emissivity = material.emissivity * material.emissiveIntensity;
      record.specular = material.specular;
      record.metallic = material.metallic;
      record.opacity = material.opacity;
      record.uvScale = material.uvScale;
      record.textureMask = textureMask;
      // The weight exactly as the G-buffer stores it, see GetShadedClearCoat. It takes the 8-bit grid
      // into unorm16 without loss.
      record.clearCoatPacked = glm::packUnorm2x16(glm::vec2(GetShadedClearCoat(material),
        std::clamp(material.clearCoatRoughness, 0.0f, 1.0f)));

      // Written for every material and read only on the instances TlasBuilder flags as
      // dielectrics, which is decided on the same transparent-and-mode rule.
      record.mediumPriority = material.mediumPriority;
      record.transmittanceTint = glm::clamp(material.transmittanceColor, glm::vec3(0.0f), glm::vec3(1.0f));
      record.ior = std::clamp(material.ior, MIN_TRANSMISSION_IOR, MAX_TRANSMISSION_IOR);
      record.absorption = -glm::log(glm::max(record.transmittanceTint, glm::vec3(MIN_TRANSMITTANCE)))
        / std::max(material.transmittanceDistance, MIN_TRANSMITTANCE_DISTANCE);

      if (IsPathTraceTransmissive(material))
        m_StagingTransmission[handle.index] = material.transmissionMode;
    });

    std::memcpy(slot.records.GetMapped(), m_Staging.data(),
      size_t(count) * sizeof(RayTracingMaterialRecord));
    slot.recordCount = count;

    // Only emitting and transmissive records fold anything, so an edit to any other material
    // never restarts the path tracer's accumulation.
    uint64_t digest = 0;
    for (uint32_t i = 0; i < count; i++)
    {
      const RayTracingMaterialRecord& record = m_Staging[i];
      const uint32_t emissionMask =
        record.textureMask & (RT_MATERIAL_EMISSIVE_SHADING | RT_MATERIAL_EMISSIVE_MAP);
      if ((emissionMask & RT_MATERIAL_EMISSIVE_SHADING) != 0)
      {
        digest = FoldWord(digest, uint64_t(i) | (uint64_t(emissionMask) << 32));
        digest = FoldWord(digest, FloatPair(record.emissivity.x, record.emissivity.y));
        digest = FoldWord(digest, uint64_t(glm::floatBitsToUint(record.emissivity.z))
          | (uint64_t(record.emissiveIndex) << 32));
        digest = FoldWord(digest, FloatPair(record.uvScale.x, record.uvScale.y));
      }

      if (m_StagingTransmission[i] != TransmissionMode::None)
      {
        digest = FoldWord(digest, uint64_t(i) | (uint64_t(m_StagingTransmission[i]) << 32)
          | (uint64_t(uint32_t(record.mediumPriority)) << 40));
        digest = FoldWord(digest, FloatPair(record.transmittanceTint.x, record.transmittanceTint.y));
        digest = FoldWord(digest, FloatPair(record.transmittanceTint.z, record.ior));
        digest = FoldWord(digest, FloatPair(record.absorption.x, record.absorption.y));
        digest = FoldWord(digest, uint64_t(glm::floatBitsToUint(record.absorption.z)));
      }
    }
    slot.pathTraceDigest = digest;
  }

  bool RayTracingMaterialTable::IsValid(uint32_t frameIndex) const
  {
    return frameIndex < m_Slots.size() && m_Slots[frameIndex].records.Get() != VK_NULL_HANDLE;
  }

  VkBuffer RayTracingMaterialTable::GetBuffer(uint32_t frameIndex) const
  {
    return frameIndex < m_Slots.size() ? m_Slots[frameIndex].records.Get() : VK_NULL_HANDLE;
  }

  VkDeviceSize RayTracingMaterialTable::GetBufferSize(uint32_t frameIndex) const
  {
    return frameIndex < m_Slots.size() ? m_Slots[frameIndex].records.GetSize() : 0;
  }

  uint32_t RayTracingMaterialTable::GetRecordCount(uint32_t frameIndex) const
  {
    return frameIndex < m_Slots.size() ? m_Slots[frameIndex].recordCount : 0;
  }

  uint64_t RayTracingMaterialTable::GetPathTraceDigest(uint32_t frameIndex) const
  {
    return frameIndex < m_Slots.size() ? m_Slots[frameIndex].pathTraceDigest : 0;
  }
}
