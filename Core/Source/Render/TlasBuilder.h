#pragma once

#include "Pch.h"
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

  // The scene's top level acceleration structure, rebuilt every frame, together with the
  // parallel record buffer that tells a shader what a hit belongs to.
  //
  // The instance list is built from the snapshot's FULL object list, the frustum-culled
  // tail included. A secondary ray leaves the camera frustum on its first bounce, so
  // off-screen geometry has to stay hittable - that is the point of tracing it at all.
  //
  // Everything is per frame in flight. The GPU can still be tracing frame N-1's structure
  // while frame N is recorded, so the structure, the instance array its build reads and
  // the record buffer a shader reads all belong to a frame slot, never to the builder.
  class TlasBuilder
  {
  public:

    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    // Fills the frame slot's instance and record buffers from the snapshot and records
    // the build into cmd. Two constraints on where it may be called:
    //  - outside any render pass instance, which a build may not be recorded inside;
    //  - only after this slot's frame fence has been waited on, because the buffers it
    //    overwrites, and may replace outright, are the ones frame N-1 read.
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

    // True when the last Build replaced this slot's record buffer, which growth does.
    // A descriptor pointing at the old handle has to be rewritten before it is used.
    bool RecordBufferChanged(uint32_t frameIndex) const;

  private:

    // 64 bytes per VkAccelerationStructureInstanceKHR and 32 per record, so 4096 covers
    // the racing scene's ~2300 structure-bearing objects before instancing for 384 KB per
    // frame slot. Growth doubles from here rather than starting at a fixed generous cap:
    // scatter instancing multiplies the object count by an amount only the scene knows,
    // so a cap large enough to be safe would be resident memory nothing usually needs.
    static constexpr uint32_t INITIAL_INSTANCE_CAPACITY = 4096;
    // 16 MB of instance array plus 8 MB of records per frame slot. A ceiling against a
    // runaway scene, not a budget the scene is expected to reach; instances past it are
    // dropped with one warning.
    static constexpr uint32_t MAX_INSTANCE_CAPACITY = 256 * 1024;

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
      // Sized with the structure and replaced with it. A build reads it for as long as
      // the frame it was recorded in runs.
      AccelerationStructureScratch scratch;
      VulkanAccelerationStructure structure;
      // Instance capacity the structure and the scratch were sized for. Capacity only
      // grows, so this is also the test for whether they have to be recreated.
      uint32_t sizedForCount = 0;
      uint32_t instanceCount = 0;
      bool built = false;
      bool recordBufferChanged = false;
    };

    // Grows both buffers to hold `required` instances, doubling up to the cap, and
    // returns the capacity that resulted. Zero means the slot has no usable buffers.
    uint32_t EnsureCapacity(const RenderContext& ctx, FrameSlot& slot, uint64_t required);

    // The mesh an object contributes geometry with, or null when it contributes none:
    // a stale mesh or material handle, or a mesh with no bottom level structure.
    static const VulkanVertexBuffer* ResolveGeometry(const RenderObject& object,
      MeshManager& meshes, MaterialManager& materials);

    std::vector<FrameSlot> m_Slots;
    bool b_CapacityWarned = false;
    bool b_FirstBuildLogged = false;
  };
}
