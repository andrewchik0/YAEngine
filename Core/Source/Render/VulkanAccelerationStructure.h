#pragma once

#include "Pch.h"
#include "VulkanBuffer.h"

namespace YAEngine
{
  struct RenderContext;

  // One indexed triangle geometry a bottom level structure is built over. Device
  // addresses, not handles: an acceleration structure build reads its input that way,
  // which is why every buffer it names carries VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT.
  struct AccelerationStructureGeometry
  {
    VkDeviceAddress vertexAddress = 0;
    VkDeviceSize vertexStride = 0;
    // Highest vertex index the build may address, so vertexCount - 1, not the count.
    uint32_t maxVertex = 0;
    VkDeviceAddress indexAddress = 0;
    uint32_t indexCount = 0;
    // Promise the any-hit shader each triangle at most once. The glass transmittance in pt_shadow.rahit
    // needs it, and without it an alpha cutout can run its texture fetch several times per triangle. The
    // promise can cost the build the spatial splits that make traversal fast, so geometry no any-hit
    // runs on goes without.
    bool noDuplicateAnyHit = false;
  };

  // Scratch memory for one acceleration structure build. What
  // minAccelerationStructureScratchOffsetAlignment constrains is the device address the
  // build is handed, not the allocation behind it, and VMA aligns the latter. The buffer
  // is therefore over-allocated by one alignment and the address rounded up inside it,
  // which holds whatever the memory happened to start at.
  struct AccelerationStructureScratch
  {
    VulkanBuffer buffer;
    VkDeviceAddress deviceAddress = 0;

    static AccelerationStructureScratch Create(const RenderContext& ctx, VkDeviceSize size);

    void Destroy(const RenderContext& ctx) { buffer.Destroy(ctx); }
  };

  // An acceleration structure together with the buffer it is stored in. The two share a
  // lifetime - the structure is a view over that buffer's memory - so Destroy() always
  // releases the handle before the storage.
  class VulkanAccelerationStructure
  {
  public:

    // Builds a bottom level structure over one triangle geometry on the same single-time
    // command buffer every staged upload uses, blocking until it has finished. Returns
    // false and leaves the object empty when the structure could not be created.
    bool BuildBottomLevel(const RenderContext& ctx, const AccelerationStructureGeometry& geometry);

    // Storage and scratch a top level build over instanceCount instances needs. Query it
    // for the capacity the instance array will ever hold rather than for one frame's
    // count: a structure created from that survives every frame that fits inside it, and
    // a build with fewer instances than it was sized for is exactly what that allows.
    static VkAccelerationStructureBuildSizesInfoKHR GetTopLevelBuildSizes(
      const RenderContext& ctx, uint32_t instanceCount);

    // Allocates storage for the size above and creates an empty top level structure over
    // it, replacing whatever this object held. Nothing is built yet.
    bool CreateTopLevel(const RenderContext& ctx, VkDeviceSize size);

    // Records a top level build into a frame command buffer. Nothing is waited on here,
    // unlike the bottom level path: the caller keeps the instance array and the scratch
    // alive until the frame this was recorded into has retired, and owns the barriers
    // around it.
    void CmdBuildTopLevel(const RenderContext& ctx, VkCommandBuffer cmd,
      VkDeviceAddress instanceAddress, uint32_t instanceCount, VkDeviceAddress scratchAddress);

    void Destroy(const RenderContext& ctx);

    VkAccelerationStructureKHR Get() const { return m_Handle; }

    // Valid from creation, not from the build: a TLAS instance can reference it either way.
    VkDeviceAddress GetDeviceAddress() const { return m_DeviceAddress; }

    bool IsValid() const { return m_Handle != VK_NULL_HANDLE; }

    VkDeviceSize GetSize() const { return m_Buffer.GetSize(); }

  private:

    // Allocates storage for the size the build sizes query reported and creates the
    // structure over it.
    bool Create(const RenderContext& ctx, VkAccelerationStructureTypeKHR type, VkDeviceSize size);

    VkAccelerationStructureKHR m_Handle {};
    VulkanBuffer m_Buffer;
    VkDeviceAddress m_DeviceAddress = 0;
  };
}
