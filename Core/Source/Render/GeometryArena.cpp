#include "GeometryArena.h"

#include "RenderContext.h"
#include "VulkanCommandBuffer.h"
#include "Utils/Log.h"

namespace YAEngine
{
  namespace
  {
    constexpr VkBufferUsageFlags POSITION_USAGE =
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
      VK_BUFFER_USAGE_TRANSFER_DST_BIT |
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    constexpr VkBufferUsageFlags INDEX_USAGE =
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
      VK_BUFFER_USAGE_TRANSFER_DST_BIT |
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    // Hands a reserved block back unless Commit() ran first. Staging allocation and
    // the queue submit that follow a reservation both throw, and a block nobody
    // returns stays occupied for the rest of the session, shrinking the arena.
    struct ArenaReservation
    {
      ArenaReservation() = default;
      ArenaReservation(const ArenaReservation&) = delete;
      ArenaReservation& operator=(const ArenaReservation&) = delete;

      ~ArenaReservation()
      {
        Release();
      }

      void Attach(VulkanBuffer& target, uint32_t blockOffset, uint32_t blockSize)
      {
        buffer = &target;
        offset = blockOffset;
        size = blockSize;
      }

      void Release()
      {
        if (buffer != nullptr)
          buffer->Free(offset, size);
        buffer = nullptr;
      }

      void Commit()
      {
        buffer = nullptr;
      }

      VulkanBuffer* buffer = nullptr;
      uint32_t offset = 0;
      uint32_t size = 0;
    };

    // Owns the staging buffer across the submit, which throws.
    struct StagingScope
    {
      ~StagingScope()
      {
        if (buffer != VK_NULL_HANDLE)
          vmaDestroyBuffer(allocator, buffer, allocation);
      }

      VmaAllocator allocator {};
      VkBuffer buffer {};
      VmaAllocation allocation {};
    };

    // Owns a VulkanBuffer that has no destructor of its own. After AdoptStorage the
    // same object holds the retired handles, which have to go either way.
    struct BufferScope
    {
      ~BufferScope()
      {
        buffer.Destroy(ctx);
      }

      const RenderContext& ctx;
      VulkanBuffer& buffer;
    };
  }

  void GeometryArena::Init(const RenderContext& ctx)
  {
    m_Positions = VulkanBuffer::CreateGpuOnly(ctx, POSITION_INITIAL_BYTES, POSITION_USAGE);
    m_Indices16 = VulkanBuffer::CreateGpuOnly(ctx, NARROW_INDEX_INITIAL_BYTES, INDEX_USAGE);
    m_Indices32 = VulkanBuffer::CreateGpuOnly(ctx, WIDE_INDEX_INITIAL_BYTES, INDEX_USAGE);

    // Without R16G16B16A16_UNORM as a vertex format nothing can read the quantized
    // stream, so it is not reserved, not staged and not even packed on the CPU.
    b_ShadowStreamEnabled = ctx.unorm16VertexSupported;
    if (b_ShadowStreamEnabled)
      m_ShadowPositions = VulkanBuffer::CreateGpuOnly(ctx, SHADOW_POSITION_INITIAL_BYTES, POSITION_USAGE);

    YA_LOG_INFO("Render",
      "Geometry arena created: positions %llu MB, shadow positions %llu MB, 16-bit indices %llu MB, 32-bit indices %llu MB",
      (unsigned long long)(POSITION_INITIAL_BYTES / (1024 * 1024)),
      (unsigned long long)((b_ShadowStreamEnabled ? SHADOW_POSITION_INITIAL_BYTES : 0) / (1024 * 1024)),
      (unsigned long long)(NARROW_INDEX_INITIAL_BYTES / (1024 * 1024)),
      (unsigned long long)(WIDE_INDEX_INITIAL_BYTES / (1024 * 1024)));
  }

  void GeometryArena::Destroy(const RenderContext& ctx)
  {
    LogUsage("shutdown");
    m_Positions.Destroy(ctx);
    m_ShadowPositions.Destroy(ctx);
    m_Indices16.Destroy(ctx);
    m_Indices32.Destroy(ctx);
  }

  void GeometryArena::NarrowIndices(const uint32_t* indices, size_t indexCount,
    std::vector<uint16_t>& out)
  {
    out.resize(indexCount);
    for (size_t i = 0; i < indexCount; i++)
      out[i] = static_cast<uint16_t>(indices[i]);
  }

  uint32_t GeometryArena::GetPositionUsedBytes() const
  {
    return m_Positions.GetAllocatorFrontier() - m_Positions.GetAllocatorFreeBytes();
  }

  uint32_t GeometryArena::GetShadowPositionUsedBytes() const
  {
    return m_ShadowPositions.GetAllocatorFrontier() - m_ShadowPositions.GetAllocatorFreeBytes();
  }

  uint32_t GeometryArena::GetIndexUsedBytes(VkIndexType indexType) const
  {
    const VulkanBuffer& buffer = IndexBuffer(indexType);
    return buffer.GetAllocatorFrontier() - buffer.GetAllocatorFreeBytes();
  }

  void GeometryArena::LogUsage(const char* reason) const
  {
    YA_LOG_INFO("Render",
      "Geometry arena (%s): positions %u/%llu KB used, high water %u KB, %zu free blocks",
      reason,
      GetPositionUsedBytes() / 1024, (unsigned long long)(GetPositionCapacityBytes() / 1024),
      m_PositionHighWater / 1024, GetPositionFreeBlockCount());

    YA_LOG_INFO("Render",
      "Geometry arena (%s): shadow positions %u/%llu KB used, high water %u KB, %zu free blocks",
      reason,
      GetShadowPositionUsedBytes() / 1024,
      (unsigned long long)(GetShadowPositionCapacityBytes() / 1024),
      m_ShadowPositionHighWater / 1024, GetShadowPositionFreeBlockCount());

    YA_LOG_INFO("Render",
      "Geometry arena (%s) indices: 16-bit %u/%llu KB used, high water %u KB, %zu free blocks; "
      "32-bit %u/%llu KB used, high water %u KB, %zu free blocks",
      reason,
      GetIndexUsedBytes(VK_INDEX_TYPE_UINT16) / 1024,
      (unsigned long long)(GetIndexCapacityBytes(VK_INDEX_TYPE_UINT16) / 1024),
      m_IndexHighWater16 / 1024, GetIndexFreeBlockCount(VK_INDEX_TYPE_UINT16),
      GetIndexUsedBytes(VK_INDEX_TYPE_UINT32) / 1024,
      (unsigned long long)(GetIndexCapacityBytes(VK_INDEX_TYPE_UINT32) / 1024),
      m_IndexHighWater32 / 1024, GetIndexFreeBlockCount(VK_INDEX_TYPE_UINT32));

    YA_LOG_INFO("Render", "Geometry arena index width: %llu KB uploaded, %llu KB saved against 32-bit everywhere",
      (unsigned long long)(m_IndexBytes / 1024),
      (unsigned long long)(GetIndexSavedBytes() / 1024));

    YA_LOG_INFO("Render", "Geometry arena shadow LOD cost: %llu KB of index memory",
      (unsigned long long)(m_LodIndexBytes / 1024));

    YA_LOG_INFO("Render",
      "Geometry arena quantization: worst position error %.3f cm on a mesh %.1f units across, both in mesh local space",
      m_MaxQuantizeError * 100.0f, m_MaxQuantizeErrorExtent);

    // The LOD deformation budget is the twin of the number above, so the two worst
    // cases of the shadow geometry path print together.
    MeshSimplifier::LogWorstError();
  }

  uint32_t GeometryArena::AllocateGrowing(const RenderContext& ctx, VulkanBuffer& buffer,
    uint32_t size, VkBufferUsageFlags usage, const char* name)
  {
    uint32_t offset = buffer.Allocate(size);
    if (offset != UINT32_MAX)
      return offset;

    VkDeviceSize oldSize = buffer.GetSize();
    VkDeviceSize newSize = oldSize;
    // The request has to fit above the bump frontier, because the free list is what
    // just failed. Doubling until it does keeps growth to O(log n) copies.
    while (newSize < ARENA_MAX_BYTES &&
      newSize - VkDeviceSize(buffer.GetAllocatorFrontier()) < VkDeviceSize(size))
    {
      newSize = std::min(newSize * 2, ARENA_MAX_BYTES);
    }

    if (newSize == oldSize || newSize - VkDeviceSize(buffer.GetAllocatorFrontier()) < VkDeviceSize(size))
    {
      YA_LOG_ERROR("Render",
        "Geometry arena %s exhausted: requested %u bytes, %llu bytes left of a %llu byte hard cap",
        name, size,
        (unsigned long long)(oldSize - VkDeviceSize(buffer.GetAllocatorFrontier())),
        (unsigned long long)ARENA_MAX_BYTES);
      return UINT32_MAX;
    }

    VulkanBuffer grown = VulkanBuffer::CreateGpuOnly(ctx, newSize, usage);
    BufferScope grownScope { .ctx = ctx, .buffer = grown };

    // The old buffer may still be referenced by in-flight command buffers. Growth
    // happens on asset load or procedural regeneration only, so a wait-idle here is
    // far cheaper than carrying a deferred-destroy queue.
    vkDeviceWaitIdle(ctx.device);

    VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();
    VkBufferCopy region {};
    region.size = oldSize;
    vkCmdCopyBuffer(cmd, buffer.Get(), grown.Get(), 1, &region);
    ctx.commandBuffer->EndSingleTimeCommands(cmd);

    // Contents land at identical offsets, so every live allocation stays valid and
    // no mesh record needs fixing up. grownScope then destroys the retired handles.
    buffer.AdoptStorage(grown);

    YA_LOG_INFO("Render", "Geometry arena %s grown from %llu MB to %llu MB",
      name,
      (unsigned long long)(oldSize / (1024 * 1024)),
      (unsigned long long)(newSize / (1024 * 1024)));

    return buffer.Allocate(size);
  }

  void GeometryArena::StageCopies(const RenderContext& ctx, const StagedCopy* copies, size_t count)
  {
    size_t totalBytes = 0;
    for (size_t i = 0; i < count; i++)
      totalBytes += copies[i].size;

    VkBufferCreateInfo stagingInfo {};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = totalBytes;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocInfo {};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    StagingScope staging;
    staging.allocator = ctx.allocator;
    if (vmaCreateBuffer(ctx.allocator, &stagingInfo, &stagingAllocInfo, &staging.buffer, &staging.allocation, nullptr) != VK_SUCCESS)
    {
      staging.buffer = VK_NULL_HANDLE;
      YA_LOG_ERROR("Render", "Failed to create geometry arena staging buffer");
      throw std::runtime_error("Failed to create geometry arena staging buffer");
    }

    void* mapped = nullptr;
    vmaMapMemory(ctx.allocator, staging.allocation, &mapped);
    size_t cursor = 0;
    for (size_t i = 0; i < count; i++)
    {
      std::memcpy(static_cast<uint8_t*>(mapped) + cursor, copies[i].data, copies[i].size);
      cursor += copies[i].size;
    }
    vmaUnmapMemory(ctx.allocator, staging.allocation);

    VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();

    cursor = 0;
    for (size_t i = 0; i < count; i++)
    {
      VkBufferCopy region {};
      region.srcOffset = cursor;
      region.dstOffset = copies[i].destinationOffset;
      region.size = copies[i].size;
      vkCmdCopyBuffer(cmd, staging.buffer, copies[i].destination, 1, &region);
      cursor += copies[i].size;
    }

    ctx.commandBuffer->EndSingleTimeCommands(cmd);
  }

  GeometryArenaAllocation GeometryArena::Upload(const RenderContext& ctx,
    const glm::vec3* positions, size_t positionCount,
    const uint32_t* indices, size_t indexCount,
    const QuantizedPositions& quantized,
    const MeshLodLevels* lods,
    GeometryArenaIndexRange* lodRanges, size_t lodRangeCount)
  {
    GeometryArenaAllocation result {};

    if (positions == nullptr || positionCount == 0 || indices == nullptr || indexCount == 0)
      return result;

    bool narrow = positionCount <= NARROW_INDEX_VERTEX_LIMIT;
    VkIndexType indexType = narrow ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    uint32_t indexStride = IndexStride(indexType);
    const char* indexBufferName = narrow ? "16-bit indices" : "32-bit indices";

    size_t positionBytes = positionCount * POSITION_STRIDE;
    size_t indexBytes = indexCount * indexStride;

    if (positionBytes > ARENA_MAX_BYTES || indexBytes > ARENA_MAX_BYTES)
    {
      YA_LOG_ERROR("Render", "Mesh too large for the geometry arena: %zu position bytes, %zu index bytes",
        positionBytes, indexBytes);
      return result;
    }

    // Rounding every allocation keeps every offset a multiple of the stride, so the
    // byte offset -> element offset conversions below are always exact.
    uint32_t positionAllocSize = RoundUp(uint32_t(positionBytes), POSITION_STRIDE);
    uint32_t indexAllocSize = RoundUp(uint32_t(indexBytes), indexStride);

    uint32_t positionOffset = AllocateGrowing(ctx, m_Positions, positionAllocSize, POSITION_USAGE, "positions");
    if (positionOffset == UINT32_MAX)
    {
      if (!b_PositionExhaustionReported)
      {
        b_PositionExhaustionReported = true;
        YA_LOG_WARN("Render", "Geometry arena position allocation failed, mesh falls back to standalone buffers");
      }
      return result;
    }

    // From here on every reservation is owned by a scope guard: the staging upload at
    // the end of this function throws on allocation failure and on submit failure.
    ArenaReservation positionReservation;
    positionReservation.Attach(m_Positions, positionOffset, positionAllocSize);

    VulkanBuffer& indexBuffer = IndexBuffer(indexType);
    uint32_t indexOffset = AllocateGrowing(ctx, indexBuffer, indexAllocSize, INDEX_USAGE, indexBufferName);
    if (indexOffset == UINT32_MAX)
    {
      if (!b_IndexExhaustionReported)
      {
        b_IndexExhaustionReported = true;
        YA_LOG_WARN("Render", "Geometry arena index allocation failed, mesh falls back to standalone buffers");
      }
      return result;
    }

    ArenaReservation indexReservation;
    indexReservation.Attach(indexBuffer, indexOffset, indexAllocSize);

    if (positionOffset % POSITION_STRIDE != 0 || indexOffset % indexStride != 0)
    {
      YA_LOG_ERROR("Render",
        "Geometry arena produced misaligned offsets: positions %u (stride %u), indices %u (stride %u)",
        positionOffset, POSITION_STRIDE, indexOffset, indexStride);
      return result;
    }

    // Optional third stream: failing to place it only costs the mesh its seat in the
    // quantized shadow path, so nothing reserved above is rolled back for it.
    size_t shadowBytes = quantized.data.size() * sizeof(uint16_t);
    uint32_t shadowOffset = UINT32_MAX;
    uint32_t shadowAllocSize = 0;
    ArenaReservation shadowReservation;

    bool quantizedUsable = b_ShadowStreamEnabled
      && quantized.data.size() == positionCount * 4
      && shadowBytes <= ARENA_MAX_BYTES;

    // A mesh whose grid is coarser than the shadow bias would drift visibly, and the
    // exact stream costs it nothing but the indirect batch.
    if (quantizedUsable && quantized.maxError > MAX_QUANTIZE_ERROR)
    {
      quantizedUsable = false;
      YA_LOG_WARN("Render",
        "Quantization error %.3f cm exceeds the %.3f cm budget on a mesh %.1f units across (%zu vertices), "
        "it keeps the exact shadow position stream",
        quantized.maxError * 100.0f, MAX_QUANTIZE_ERROR * 100.0f,
        std::max({ quantized.scale.x, quantized.scale.y, quantized.scale.z }), positionCount);
    }

    if (quantizedUsable)
    {
      shadowAllocSize = RoundUp(uint32_t(shadowBytes), SHADOW_POSITION_STRIDE);
      shadowOffset = AllocateGrowing(ctx, m_ShadowPositions, shadowAllocSize, POSITION_USAGE,
        "shadow positions");

      if (shadowOffset != UINT32_MAX)
        shadowReservation.Attach(m_ShadowPositions, shadowOffset, shadowAllocSize);

      if (shadowOffset != UINT32_MAX && shadowOffset % SHADOW_POSITION_STRIDE != 0)
      {
        YA_LOG_ERROR("Render",
          "Geometry arena produced a misaligned shadow position offset: %u (stride %u)",
          shadowOffset, SHADOW_POSITION_STRIDE);
        shadowReservation.Release();
        shadowOffset = UINT32_MAX;
      }

      if (shadowOffset == UINT32_MAX && !b_ShadowExhaustionReported)
      {
        b_ShadowExhaustionReported = true;
        YA_LOG_WARN("Render",
          "Geometry arena shadow position allocation failed, the mesh falls out of the quantized shadow path");
      }
    }

    // Shadow LOD levels are extra index ranges over the vertices reserved above, so
    // they cost no position memory and travel with the mesh they belong to.
    ArenaReservation lodReservations[MeshSimplifier::LOD_COUNT - 1];
    std::vector<uint16_t> narrowLodIndices[MeshSimplifier::LOD_COUNT - 1];
    size_t lodByteSizes[MeshSimplifier::LOD_COUNT - 1] {};
    // Handed to the caller only once the upload has gone through, so a throw leaves
    // no range pointing at a block the guards have already given back.
    GeometryArenaIndexRange stagedRanges[MeshSimplifier::LOD_COUNT - 1] {};

    size_t lodLevelCount = lods != nullptr
      ? std::min({ lods->levels.size(), lodRangeCount, std::size(lodReservations) })
      : size_t(0);

    for (size_t level = 0; level < lodLevelCount; level++)
    {
      const auto& levelIndices = lods->levels[level];
      if (levelIndices.empty())
        continue;

      size_t lodBytes = levelIndices.size() * indexStride;
      if (lodBytes > ARENA_MAX_BYTES)
        continue;

      uint32_t lodAllocSize = RoundUp(uint32_t(lodBytes), indexStride);
      // The mesh's own index buffer, never a fresh choice: a level in the other one
      // would break the contiguous indirect command range of its tile.
      uint32_t lodOffset = AllocateGrowing(ctx, indexBuffer, lodAllocSize, INDEX_USAGE, indexBufferName);
      if (lodOffset == UINT32_MAX)
        continue;

      lodReservations[level].Attach(indexBuffer, lodOffset, lodAllocSize);

      if (lodOffset % indexStride != 0)
      {
        YA_LOG_ERROR("Render", "Geometry arena produced a misaligned index offset: %u (stride %u)",
          lodOffset, indexStride);
        lodReservations[level].Release();
        continue;
      }

      if (narrow)
        NarrowIndices(levelIndices.data(), levelIndices.size(), narrowLodIndices[level]);

      lodByteSizes[level] = lodBytes;

      stagedRanges[level].byteOffset = lodOffset;
      stagedRanges[level].byteSize = lodAllocSize;
      stagedRanges[level].firstIndex = lodOffset / indexStride;
      stagedRanges[level].indexCount = uint32_t(levelIndices.size());
      stagedRanges[level].indexType = indexType;
      stagedRanges[level].resident = true;
    }

    std::vector<uint16_t> narrowIndices;
    if (narrow)
      NarrowIndices(indices, indexCount, narrowIndices);

    const void* indexData = narrow
      ? static_cast<const void*>(narrowIndices.data())
      : static_cast<const void*>(indices);

    // Buffer handles are read only here: growing an arena swaps its VkBuffer, and
    // every reservation above can be the one that triggers the growth.
    StagedCopy copies[3 + MeshSimplifier::LOD_COUNT - 1] {};
    size_t copyCount = 0;
    copies[copyCount++] = { positions, positionBytes, m_Positions.Get(), positionOffset };
    copies[copyCount++] = { indexData, indexBytes, indexBuffer.Get(), indexOffset };
    if (shadowOffset != UINT32_MAX)
      copies[copyCount++] = { quantized.data.data(), shadowBytes, m_ShadowPositions.Get(), shadowOffset };

    for (size_t level = 0; level < lodLevelCount; level++)
    {
      if (!stagedRanges[level].resident)
        continue;

      const void* lodData = narrow
        ? static_cast<const void*>(narrowLodIndices[level].data())
        : static_cast<const void*>(lods->levels[level].data());
      copies[copyCount++] = { lodData, lodByteSizes[level], indexBuffer.Get(), stagedRanges[level].byteOffset };
    }

    StageCopies(ctx, copies, copyCount);

    positionReservation.Commit();
    indexReservation.Commit();
    shadowReservation.Commit();
    for (auto& reservation : lodReservations)
      reservation.Commit();

    for (size_t level = 0; level < lodLevelCount; level++)
      lodRanges[level] = stagedRanges[level];

    result.positionByteOffset = positionOffset;
    result.positionByteSize = positionAllocSize;
    result.indexByteOffset = indexOffset;
    result.indexByteSize = indexAllocSize;
    result.vertexOffset = positionOffset / POSITION_STRIDE;
    result.firstIndex = indexOffset / indexStride;
    result.indexCount = uint32_t(indexCount);
    result.indexType = indexType;
    result.resident = true;

    if (shadowOffset != UINT32_MAX)
    {
      result.shadowPositionByteOffset = shadowOffset;
      result.shadowPositionByteSize = shadowAllocSize;
      result.shadowVertexOffset = shadowOffset / SHADOW_POSITION_STRIDE;
      result.dequantScale = quantized.scale;
      result.dequantBias = quantized.bias;
      result.shadowPositionsResident = true;

      m_ShadowPositionHighWater = std::max(m_ShadowPositionHighWater,
        m_ShadowPositions.GetAllocatorFrontier());

      if (quantized.maxError > m_MaxQuantizeError)
      {
        m_MaxQuantizeError = quantized.maxError;
        m_MaxQuantizeErrorExtent = std::max({ quantized.scale.x, quantized.scale.y, quantized.scale.z });
      }
    }

    for (size_t level = 0; level < lodLevelCount; level++)
    {
      if (stagedRanges[level].resident)
        m_LodIndexBytes += lodByteSizes[level];
    }

    m_PositionHighWater = std::max(m_PositionHighWater, m_Positions.GetAllocatorFrontier());
    uint32_t& indexHighWater = narrow ? m_IndexHighWater16 : m_IndexHighWater32;
    indexHighWater = std::max(indexHighWater, indexBuffer.GetAllocatorFrontier());
    m_IndexBytes += indexBytes;
    m_WideIndexBytes += indexCount * sizeof(uint32_t);
    m_ContentVersion++;

    return result;
  }

  void GeometryArena::Free(GeometryArenaAllocation& allocation)
  {
    if (!allocation.resident)
      return;

    m_Positions.Free(allocation.positionByteOffset, allocation.positionByteSize);
    IndexBuffer(allocation.indexType).Free(allocation.indexByteOffset, allocation.indexByteSize);
    if (allocation.shadowPositionsResident)
      m_ShadowPositions.Free(allocation.shadowPositionByteOffset, allocation.shadowPositionByteSize);

    // Symmetric with the increments in Upload, otherwise the width saving keeps
    // counting meshes that a regeneration already threw away.
    m_IndexBytes -= size_t(allocation.indexCount) * IndexStride(allocation.indexType);
    m_WideIndexBytes -= size_t(allocation.indexCount) * sizeof(uint32_t);

    allocation = {};
    m_ContentVersion++;
  }

  void GeometryArena::FreeIndices(GeometryArenaIndexRange& range)
  {
    if (!range.resident)
      return;

    IndexBuffer(range.indexType).Free(range.byteOffset, range.byteSize);
    m_LodIndexBytes -= size_t(range.indexCount) * IndexStride(range.indexType);
    range = {};
    m_ContentVersion++;
  }
}
