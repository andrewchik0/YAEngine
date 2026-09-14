#pragma once

#include "Pch.h"
#include "RayTracingMaterialData.h"
#include "VulkanBuffer.h"

namespace YAEngine
{
  struct RenderContext;
  class MaterialManager;
  class TextureManager;

  static_assert(sizeof(RayTracingMaterialRecord) == 80,
    "RayTracingMaterialRecord no longer matches its std430 layout");

  // The scene's materials as one SSBO a hit can index with the material slot the instance
  // record carries, with every texture already resolved to a bindless table slot.
  //
  // Rebuilt whole every frame rather than dirty tracked. A scene holds hundreds of
  // materials, so the rewrite is tens of kilobytes of memcpy, while dirty tracking would
  // mean a per-frame-in-flight generation per material and a second place for the editor's
  // edits to be missed from. The tradeoff flips only at tens of thousands of materials.
  //
  // Per frame in flight for the same reason the TLAS records are: frame N-1 can still be
  // tracing while frame N is recorded, and this buffer is host mapped. Like the TLAS, editor
  // builds keep one extra bake slot past them, for bakes that trace outside the frame loop.
  class RayTracingMaterialTable
  {
  public:

    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    // Refills the slot from the material manager. Must run only once nothing that reads
    // this slot is still in flight (for a frame slot, after its frame fence), and before
    // anything that reads the buffer is recorded - the host write is ordered against the
    // read by the barrier TlasBuilder already issues for its own host writes.
    void Update(const RenderContext& ctx, uint32_t frameIndex,
      MaterialManager& materials, TextureManager& textures);

    bool IsValid(uint32_t frameIndex) const;

    VkBuffer GetBuffer(uint32_t frameIndex) const;
    VkDeviceSize GetBufferSize(uint32_t frameIndex) const;

    // Records actually written this frame. The buffer beyond it is zeroed padding.
    uint32_t GetRecordCount(uint32_t frameIndex) const;

#ifdef YA_EDITOR
    // Past every frame index, so the frame loop never updates it.
    uint32_t GetBakeSlot() const { return m_BakeSlot; }
#endif

  private:

    // 20 KB per frame slot, which covers every scene the engine ships with. Growth doubles
    // from here.
    static constexpr uint32_t INITIAL_CAPACITY = 256;
    // Materials cannot multiply the way scatter instancing multiplies objects, so this is
    // a guard against a runaway import rather than a budget. 5 MB per frame slot.
    static constexpr uint32_t MAX_CAPACITY = 64 * 1024;

    struct FrameSlot
    {
      VulkanBuffer records;
      uint32_t recordCount = 0;
    };

    // Grows the slot's buffer to hold `required` records, doubling up to the cap. Returns
    // the resulting capacity, zero when the slot has no usable buffer.
    uint32_t EnsureCapacity(const RenderContext& ctx, FrameSlot& slot, uint32_t required);

    std::vector<FrameSlot> m_Slots;
#ifdef YA_EDITOR
    uint32_t m_BakeSlot = 0;
#endif
    // Staging for one frame's records. The mapped buffer is write-combined, so the table is
    // assembled here and copied across in one go rather than field by field.
    std::vector<RayTracingMaterialRecord> m_Staging;
    bool b_CapacityWarned = false;
  };
}
