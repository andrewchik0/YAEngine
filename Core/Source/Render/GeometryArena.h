#pragma once

#include "Pch.h"
#include "VulkanBuffer.h"
#include "Utils/MeshSimplifier.h"
#include "Utils/PositionQuantizer.h"

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

    // The same vertices in the quantized shadow stream. It has its own allocator, so
    // it has its own vertex offset; the index ranges above serve both streams.
    uint32_t shadowPositionByteOffset = 0;
    uint32_t shadowPositionByteSize = 0;
    uint32_t shadowVertexOffset = 0;
    // Restores a quantized position to mesh local space. Meant to be folded into the
    // model matrix on the CPU, never applied in the shader.
    glm::vec3 dequantScale { 1.0f };
    glm::vec3 dequantBias { 0.0f };
    bool shadowPositionsResident = false;
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
    // Always the index type of the mesh the range belongs to. A level that landed in
    // the other index buffer would split the contiguous indirect command range its
    // tile draws from, so the caller must never mix the two.
    VkIndexType indexType = VK_INDEX_TYPE_UINT32;
    bool resident = false;
  };

  // One shared positions buffer plus one shared index buffer for every mesh in the
  // scene, so a whole pass can bind geometry once and vary only the draw arguments.
  class GeometryArena
  {
  public:

    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    // Copies one mesh into the arena, together with the shadow LOD index streams that
    // index the same vertices. Returns a non-resident allocation when the arena cannot
    // grow far enough; the caller then keeps its own buffers. An empty quantized
    // stream, or one the shadow buffer has no room for, only costs the mesh its place
    // in the quantized shadow path - the rest of the allocation stands, and so does a
    // LOD level the index arena had no room for.
    // Every stream rides one staging buffer and one submit: a stream of its own would
    // cost another queue submit and blocking fence wait per mesh.
    // lods may be null, and lodRanges must have room for lodRangeCount entries.
    GeometryArenaAllocation Upload(const RenderContext& ctx,
      const glm::vec3* positions, size_t positionCount,
      const uint32_t* indices, size_t indexCount,
      const QuantizedPositions& quantized,
      const MeshLodLevels* lods,
      GeometryArenaIndexRange* lodRanges, size_t lodRangeCount);

    // Returns the blocks to the free list for immediate reuse. The caller must
    // guarantee that no command buffer still in flight references them: the next
    // Upload can be handed the same bytes and overwrite them, and the staged copy
    // waits only on its own transfer fence. Every runtime deletion site satisfies
    // this today by deferring the free by DESTROY_DELAY_FRAMES or by calling
    // Render::WaitIdle first.
    void Free(GeometryArenaAllocation& allocation);

    // Same in-flight contract as Free().
    void FreeIndices(GeometryArenaIndexRange& range);

    VkBuffer GetPositionBuffer() const { return m_Positions.Get(); }

    // Quantized positions, read by the indirect shadow path only. Never bind this in
    // the depth prepass: the prepass writes the depth the G-buffer then tests with
    // GREATER_OR_EQUAL, and any deviation from the exact stream punches holes in it.
    VkBuffer GetShadowPositionBuffer() const { return m_ShadowPositions.Get(); }

    VkBuffer GetIndexBuffer(VkIndexType indexType) const
    {
      return IndexBuffer(indexType).Get();
    }

    uint32_t GetPositionUsedBytes() const;
    uint32_t GetShadowPositionUsedBytes() const;
    uint32_t GetIndexUsedBytes(VkIndexType indexType) const;
    VkDeviceSize GetPositionCapacityBytes() const { return m_Positions.GetSize(); }
    VkDeviceSize GetShadowPositionCapacityBytes() const { return m_ShadowPositions.GetSize(); }
    uint32_t GetShadowPositionHighWaterBytes() const { return m_ShadowPositionHighWater; }
    size_t GetShadowPositionFreeBlockCount() const { return m_ShadowPositions.GetAllocatorFreeBlockCount(); }
    // Worst position error any resident mesh pays for quantization, and the size of
    // the mesh that pays it. The pair is what tells whether 16 bits still suffice.
    // Both are in mesh local units: the grid is built from the mesh AABB and the
    // instance scale is applied only later, so an instance scaled above 1 drifts
    // proportionally further than this reports.
    float GetMaxQuantizeError() const { return m_MaxQuantizeError; }
    float GetMaxQuantizeErrorExtent() const { return m_MaxQuantizeErrorExtent; }
    VkDeviceSize GetIndexCapacityBytes(VkIndexType indexType) const { return IndexBuffer(indexType).GetSize(); }
    size_t GetPositionFreeBlockCount() const { return m_Positions.GetAllocatorFreeBlockCount(); }
    size_t GetIndexFreeBlockCount(VkIndexType indexType) const { return IndexBuffer(indexType).GetAllocatorFreeBlockCount(); }
    uint32_t GetPositionHighWaterBytes() const { return m_PositionHighWater; }
    uint32_t GetIndexHighWaterBytes(VkIndexType indexType) const
    {
      return indexType == VK_INDEX_TYPE_UINT16 ? m_IndexHighWater16 : m_IndexHighWater32;
    }
    // What the narrow buffer saves against the single 32-bit buffer this arena used
    // to bind for every mesh, so the price of the split stays visible.
    uint64_t GetIndexSavedBytes() const { return m_WideIndexBytes - m_IndexBytes; }
    uint64_t GetLodIndexBytes() const { return m_LodIndexBytes; }
    // Bumped on every successful upload or free. The shadow cache keys on it:
    // async loading can make a mesh arena-resident after its entity already sat
    // in the snapshot, changing shadow depth with no snapshot digest change.
    uint64_t GetContentVersion() const { return m_ContentVersion; }

    void LogUsage(const char* reason) const;

    static constexpr uint32_t POSITION_STRIDE = uint32_t(sizeof(glm::vec3));
    static constexpr uint32_t SHADOW_POSITION_STRIDE = PositionQuantizer::STRIDE;

    // A mesh at or below this many vertices indexes its triangles with 16 bits. The
    // index is local to the mesh - vkCmdDrawIndexed adds vertexOffset after the fetch -
    // so where the mesh sits in the shared buffer never narrows the limit. Primitive
    // restart is disabled everywhere, which keeps 0xFFFF a valid index.
    static constexpr size_t NARROW_INDEX_VERTEX_LIMIT = 65536;

    static constexpr uint32_t IndexStride(VkIndexType indexType)
    {
      return indexType == VK_INDEX_TYPE_UINT16 ? uint32_t(sizeof(uint16_t)) : uint32_t(sizeof(uint32_t));
    }

  private:

    // Sized from the measured high-water of the cafe scene (1632 meshes): 26 MB of
    // positions against 37 MB of indices when every mesh was forced to 32 bits.
    // Almost every mesh fits the narrow limit, so the 16-bit buffer inherits most of
    // that reservation and the 32-bit one keeps only what the few large meshes need.
    static constexpr VkDeviceSize POSITION_INITIAL_BYTES = 32ull * 1024 * 1024;
    static constexpr VkDeviceSize NARROW_INDEX_INITIAL_BYTES = 24ull * 1024 * 1024;
    static constexpr VkDeviceSize WIDE_INDEX_INITIAL_BYTES = 8ull * 1024 * 1024;
    // The same vertices at 8 bytes instead of 12, so two thirds of the position
    // reservation covers the same scene.
    static constexpr VkDeviceSize SHADOW_POSITION_INITIAL_BYTES = 24ull * 1024 * 1024;
    // uint32_t byte offsets and a signed vertexOffset both stay exact well below
    // this, and a single VkBuffer this large is already far past any real scene.
    static constexpr VkDeviceSize ARENA_MAX_BYTES = 1024ull * 1024 * 1024;

    // Past this a quantized position drifts further than the shadow bias can hide, so
    // the mesh keeps the exact stream and the legacy per-draw path instead. The grid
    // is built from the mesh AABB, so the error is extent/131070 per axis: 1 cm is a
    // local extent of about 1.3 km. Mesh local units, like QuantizedPositions::
    // maxError - the instance scale is not known here, and the cascade 0 budget it is
    // measured against, normalBias = texelWorldSize * 1.5, is about 2.2 cm at the
    // default shadowDistance of 200, so half of it is left as scale headroom.
    static constexpr float MAX_QUANTIZE_ERROR = 0.01f;

    static uint32_t RoundUp(uint32_t value, uint32_t multiple)
    {
      return (value + multiple - 1) / multiple * multiple;
    }

    VulkanBuffer& IndexBuffer(VkIndexType indexType)
    {
      return indexType == VK_INDEX_TYPE_UINT16 ? m_Indices16 : m_Indices32;
    }

    const VulkanBuffer& IndexBuffer(VkIndexType indexType) const
    {
      return indexType == VK_INDEX_TYPE_UINT16 ? m_Indices16 : m_Indices32;
    }

    // Packs a source index stream into the 16-bit form the narrow buffer stores.
    static void NarrowIndices(const uint32_t* indices, size_t indexCount,
      std::vector<uint16_t>& out);

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
    // Quantized duplicate of m_Positions. It exists instead of replacing the exact
    // stream because the depth prepass has to stay bit-identical to the G-buffer,
    // which rules the quantized form out for every pass but the shadow atlas.
    VulkanBuffer m_ShadowPositions;
    // One buffer per index width. A pass binds whichever one the mesh was uploaded
    // into, so a small mesh never pays for indices it cannot use.
    VulkanBuffer m_Indices16;
    VulkanBuffer m_Indices32;
    uint32_t m_PositionHighWater = 0;
    uint32_t m_ShadowPositionHighWater = 0;
    uint32_t m_IndexHighWater16 = 0;
    uint32_t m_IndexHighWater32 = 0;
    // Index bytes actually uploaded against what a single 32-bit buffer would have
    // taken, so the saving of the split stays visible.
    uint64_t m_IndexBytes = 0;
    uint64_t m_WideIndexBytes = 0;
    // Index bytes currently held by shadow LOD levels alone, so the price of the
    // feature stays visible next to what it saves.
    uint64_t m_LodIndexBytes = 0;
    float m_MaxQuantizeError = 0.0f;
    float m_MaxQuantizeErrorExtent = 0.0f;
    uint64_t m_ContentVersion = 0;
    // Nothing can read a UNORM16 vertex stream on a device without the format, so the
    // shadow buffer is never even reserved there.
    bool b_ShadowStreamEnabled = false;
    // One latch per arena: a shared one would let whichever ran out first silence the
    // others for the rest of the session.
    bool b_PositionExhaustionReported = false;
    bool b_IndexExhaustionReported = false;
    bool b_ShadowExhaustionReported = false;
  };
}
