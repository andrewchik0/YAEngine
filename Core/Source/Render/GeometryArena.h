#pragma once

#include "Pch.h"
#include "VulkanBuffer.h"

namespace YAEngine
{
  struct RenderContext;

  // Where one mesh lives inside the arena. Byte offsets drive Free(), element
  // offsets drive draws: vertexOffset is measured in vertices and firstIndex in
  // indices, matching both vkCmdDrawIndexed and VkDrawIndexedIndirectCommand.
  struct GeometryArenaAllocation
  {
    uint32_t positionByteOffset = 0;
    uint32_t positionByteSize = 0;
    uint32_t indexByteOffset = 0;
    uint32_t indexByteSize = 0;
    uint32_t vertexOffset = 0;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    // Carried per allocation rather than assumed by call sites, so a narrow index
    // arena can be added later without touching every draw.
    VkIndexType indexType = VK_INDEX_TYPE_UINT32;
    bool resident = false;
  };

  // An extra index range over the vertices of an existing allocation. A shadow LOD
  // level indexes the same positions and varies only firstIndex and indexCount, so
  // it never costs position memory.
  struct GeometryArenaIndexRange
  {
    uint32_t byteOffset = 0;
    uint32_t byteSize = 0;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    bool resident = false;
  };

  // One shared positions buffer plus one shared index buffer for every mesh in the
  // scene, so a whole pass can bind geometry once and vary only the draw arguments.
  class GeometryArena
  {
  public:

    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    // Copies one mesh into the arena. Returns a non-resident allocation when the
    // arena cannot grow far enough; the caller then keeps its own buffers.
    GeometryArenaAllocation Upload(const RenderContext& ctx,
      const glm::vec3* positions, size_t positionCount,
      const uint32_t* indices, size_t indexCount);

    // Copies one more index stream for a mesh whose positions are already resident.
    // Returns a non-resident range when the index arena cannot grow far enough; the
    // caller then simply has one LOD level fewer.
    GeometryArenaIndexRange UploadIndices(const RenderContext& ctx,
      const uint32_t* indices, size_t indexCount);

    void Free(GeometryArenaAllocation& allocation);

    void FreeIndices(GeometryArenaIndexRange& range);

    VkBuffer GetPositionBuffer() const { return m_Positions.Get(); }

    VkBuffer GetIndexBuffer(VkIndexType indexType) const
    {
      if (indexType != VK_INDEX_TYPE_UINT32)
        return VK_NULL_HANDLE;
      return m_Indices.Get();
    }

    uint32_t GetPositionUsedBytes() const;
    uint32_t GetIndexUsedBytes() const;
    VkDeviceSize GetPositionCapacityBytes() const { return m_Positions.GetSize(); }
    VkDeviceSize GetIndexCapacityBytes() const { return m_Indices.GetSize(); }
    size_t GetPositionFreeBlockCount() const { return m_Positions.GetAllocatorFreeBlockCount(); }
    size_t GetIndexFreeBlockCount() const { return m_Indices.GetAllocatorFreeBlockCount(); }
    uint32_t GetPositionHighWaterBytes() const { return m_PositionHighWater; }
    uint32_t GetIndexHighWaterBytes() const { return m_IndexHighWater; }
    uint64_t GetIndexWideningBytes() const { return m_IndexBytes - m_NarrowIndexBytes; }
    uint64_t GetLodIndexBytes() const { return m_LodIndexBytes; }

    void LogUsage(const char* reason) const;

    static constexpr uint32_t POSITION_STRIDE = uint32_t(sizeof(glm::vec3));
    static constexpr uint32_t INDEX_STRIDE = uint32_t(sizeof(uint32_t));

  private:

    // Sized from the measured high-water of the cafe scene (1632 meshes): 26 MB of
    // positions against 37 MB of 32-bit indices. Indices dominate because a welded
    // position stream is 12 bytes per vertex while a triangle costs 12 bytes of
    // indices on its own, so the index arena gets the larger reservation.
    static constexpr VkDeviceSize POSITION_INITIAL_BYTES = 32ull * 1024 * 1024;
    static constexpr VkDeviceSize INDEX_INITIAL_BYTES = 48ull * 1024 * 1024;
    // uint32_t byte offsets and a signed vertexOffset both stay exact well below
    // this, and a single VkBuffer this large is already far past any real scene.
    static constexpr VkDeviceSize ARENA_MAX_BYTES = 1024ull * 1024 * 1024;

    static uint32_t RoundUp(uint32_t value, uint32_t multiple)
    {
      return (value + multiple - 1) / multiple * multiple;
    }

    // Allocates from buffer, growing it when the free list and the bump frontier
    // are both exhausted. Returns UINT32_MAX when the hard cap is reached.
    uint32_t AllocateGrowing(const RenderContext& ctx, VulkanBuffer& buffer,
      uint32_t size, VkBufferUsageFlags usage, const char* name);

    struct StagedCopy
    {
      const void* data;
      size_t size;
      VkBuffer destination;
      uint32_t destinationOffset;
    };

    // Pushes every copy through one staging buffer and one submit: a staged upload
    // costs a queue submit plus a fence wait, and meshes arrive one at a time.
    static void StageCopies(const RenderContext& ctx, const StagedCopy* copies, size_t count);

    VulkanBuffer m_Positions;
    VulkanBuffer m_Indices;
    uint32_t m_PositionHighWater = 0;
    uint32_t m_IndexHighWater = 0;
    // The arena binds one index buffer for the whole pass, so every mesh pays for
    // 32-bit indices. These two track what the old per-mesh 16-bit choice would
    // have cost, so the price of the single index type stays visible.
    uint64_t m_IndexBytes = 0;
    uint64_t m_NarrowIndexBytes = 0;
    // Index bytes currently held by shadow LOD levels alone, so the price of the
    // feature stays visible next to what it saves.
    uint64_t m_LodIndexBytes = 0;
    bool b_ExhaustionReported = false;
  };
}
