#include "RayTracingMaterialTable.h"

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
    });

    std::memcpy(slot.records.GetMapped(), m_Staging.data(),
      size_t(count) * sizeof(RayTracingMaterialRecord));
    slot.recordCount = count;
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
}
