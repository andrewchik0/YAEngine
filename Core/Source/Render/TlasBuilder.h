#pragma once

#include "Pch.h"
#include "EmissiveLightData.h"
#include "RayTracingInstanceData.h"
#include "VulkanAccelerationStructure.h"
#include "VulkanBuffer.h"

namespace YAEngine
{
  struct RenderContext;
  struct RenderObject;
  struct SceneSnapshot;
  class MaterialManager;
  class MeshManager;
  class VulkanVertexBuffer;

  static_assert(sizeof(RayTracingInstanceRecord) == 80,
    "RayTracingInstanceRecord no longer matches its std430 layout");
  static_assert(sizeof(EmissiveLightTableHeader) == 16,
    "EmissiveLightTableHeader no longer matches its std430 layout");
  static_assert(sizeof(EmissiveLightRecord) == 80 && offsetof(EmissiveLightRecord, objectToWorld) == 16
    && offsetof(EmissiveLightRecord, aliasThreshold) == 64,
    "EmissiveLightRecord no longer matches its std430 layout");

  // The scene's top level acceleration structure, rebuilt every frame, together with the
  // parallel record buffer that tells a shader what a hit belongs to, and the emissive light
  // table next event estimation samples emitting instances from.
  //
  // The instance list is built from the snapshot's FULL object list, the frustum-culled
  // tail included. A secondary ray leaves the camera frustum on its first bounce, so
  // off-screen geometry has to stay hittable - that is the point of tracing it at all.
  //
  // Everything is per frame in flight. The GPU can still be tracing frame N-1's structure
  // while frame N is recorded, so the structure, the instance array its build reads and
  // the record buffer a shader reads all belong to a frame slot, never to the builder.
  // Editor builds keep one more slot past the frames in flight for bakes, which build and
  // trace on single-time command buffers outside the frame loop and must not touch a slot
  // the loop may still own - the shadow model buffers keep a bake slot for the same reason.
  class TlasBuilder
  {
  public:

    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    // Fills the frame slot's instance, record and emissive light buffers from the snapshot and
    // records the build into cmd. Two constraints on where it may be called:
    //  - outside any render pass instance, which a build may not be recorded inside;
    //  - only once nothing that read this slot is still in flight - for a frame slot, after
    //    its frame fence - because the buffers it overwrites may be replaced outright.
    void Build(const RenderContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
      const SceneSnapshot& snapshot, MeshManager& meshes, MaterialManager& materials);

    // False when this slot holds nothing traceable for the frame just recorded: no ray
    // tracing on this device, a scene with no ray traceable geometry, or a build that
    // failed. A consumer must test it before it binds anything below.
    bool IsValid(uint32_t frameIndex) const;

    VkAccelerationStructureKHR Get(uint32_t frameIndex) const;

    // One RayTracingInstanceRecord per TLAS instance, indexed by the instance's
    // instanceCustomIndex.
    VkBuffer GetRecordBuffer(uint32_t frameIndex) const;
    VkDeviceSize GetRecordBufferSize(uint32_t frameIndex) const;

    uint32_t GetInstanceCount(uint32_t frameIndex) const;

    // An EmissiveLightTableHeader followed by one EmissiveLightRecord per instance whose material
    // can produce an emissive texel, written by the same Build as the records. Always a bindable
    // buffer: a slot with no emitters, or one whose last Build returned early, holds a count of zero.
    // Growth replaces it like the record buffer, so a consumer rewrites its descriptor per use.
    VkBuffer GetEmissiveBuffer(uint32_t frameIndex) const;
    VkDeviceSize GetEmissiveBufferSize(uint32_t frameIndex) const;
    uint32_t GetEmissiveCount(uint32_t frameIndex) const;

#ifdef YA_EDITOR
    // Past every frame index, so the frame loop never builds into it.
    uint32_t GetBakeSlot() const { return m_BakeSlot; }
#endif

  private:

    // 64 bytes per VkAccelerationStructureInstanceKHR and 80 per record, so 4096 covers
    // the racing scene's ~2300 structure-bearing objects before instancing for 576 KB per
    // frame slot. Growth doubles from here rather than starting at a fixed generous cap:
    // scatter instancing multiplies the object count by an amount only the scene knows,
    // so a cap large enough to be safe would be resident memory nothing usually needs.
    static constexpr uint32_t INITIAL_INSTANCE_CAPACITY = 4096;
    // 16 MB of instance array plus 20 MB of records per frame slot. A ceiling against a
    // runaway scene, not a budget the scene is expected to reach; instances past it are
    // dropped with one warning.
    static constexpr uint32_t MAX_INSTANCE_CAPACITY = 256 * 1024;
    // Emitting instances are a small share of a scene, so the emissive table starts at 20 KB per
    // frame slot and doubles from there, up to MAX_INSTANCE_CAPACITY - it can never hold more
    // entries than there are instances.
    static constexpr uint32_t INITIAL_EMISSIVE_CAPACITY = 256;

    struct FrameSlot
    {
      // Build input, written straight through its host mapping in TLAS instance order.
      VulkanBuffer instances;
      // Cached with the buffer: the build wants an address, and it has to be 16-byte
      // aligned, which is a property of the allocation and so is checked once per one
      // rather than trusted or re-tested every frame.
      VkDeviceAddress instanceAddress = 0;
      bool addressUsable = false;
      // One record per instance above, at the same index.
      VulkanBuffer records;
      // The emissive light table header and records, host mapped like the records.
      VulkanBuffer emissive;
      uint32_t emissiveCount = 0;
      // Sized with the structure and replaced with it. A build reads it for as long as
      // the frame it was recorded in runs.
      AccelerationStructureScratch scratch;
      VulkanAccelerationStructure structure;
      // Instance capacity the structure and the scratch were sized for. Capacity only
      // grows, so this is also the test for whether they have to be recreated.
      uint32_t sizedForCount = 0;
      uint32_t instanceCount = 0;
      bool built = false;
    };

    // Grows both buffers to hold `required` instances, doubling up to the cap, and
    // returns the capacity that resulted. Zero means the slot has no usable buffers.
    uint32_t EnsureCapacity(const RenderContext& ctx, FrameSlot& slot, uint64_t required);

    // Grows the emissive light buffer to hold `required` records the same way. A new buffer
    // starts with a zeroed header, which is a valid empty table.
    void EnsureEmissiveCapacity(const RenderContext& ctx, FrameSlot& slot, uint64_t required);

    // Builds the alias table over the staged emissive records and writes header and records
    // into the slot.
    void UploadEmissiveTable(const RenderContext& ctx, FrameSlot& slot);
    void BuildEmissiveAliasTable();

    // The mesh an object contributes geometry with, or null when it contributes none:
    // a stale mesh or material handle, or a mesh with no bottom level structure.
    static const VulkanVertexBuffer* ResolveGeometry(const RenderObject& object,
      MeshManager& meshes, MaterialManager& materials);

    std::vector<FrameSlot> m_Slots;
#ifdef YA_EDITOR
    uint32_t m_BakeSlot = 0;
#endif
    // One Build's emissive records and their selection weights, assembled here because the
    // alias table needs all weights before anything is written, and because the mapped buffer
    // is write-combined. The alias work lists are kept too; all of them are reused every frame.
    std::vector<EmissiveLightRecord> m_EmissiveStaging;
    std::vector<double> m_EmissiveWeights;
    std::vector<double> m_AliasScaled;
    std::vector<uint32_t> m_AliasSmall;
    std::vector<uint32_t> m_AliasLarge;
    // One-time diagnostics of the frame slots only; a bake build logs its own summary.
    bool b_CapacityWarned = false;
    bool b_FirstBuildLogged = false;
  };
}
