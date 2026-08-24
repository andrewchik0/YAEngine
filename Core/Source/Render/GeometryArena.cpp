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
  }

  void GeometryArena::Init(const RenderContext& ctx)
  {
    m_Positions = VulkanBuffer::CreateGpuOnly(ctx, POSITION_INITIAL_BYTES, POSITION_USAGE);
    m_ShadowPositions = VulkanBuffer::CreateGpuOnly(ctx, SHADOW_POSITION_INITIAL_BYTES, POSITION_USAGE);
    m_Indices16 = VulkanBuffer::CreateGpuOnly(ctx, NARROW_INDEX_INITIAL_BYTES, INDEX_USAGE);
    m_Indices32 = VulkanBuffer::CreateGpuOnly(ctx, WIDE_INDEX_INITIAL_BYTES, INDEX_USAGE);

    YA_LOG_INFO("Render",
      "Geometry arena created: positions %llu MB, shadow positions %llu MB, 16-bit indices %llu MB, 32-bit indices %llu MB",
      (unsigned long long)(POSITION_INITIAL_BYTES / (1024 * 1024)),
      (unsigned long long)(SHADOW_POSITION_INITIAL_BYTES / (1024 * 1024)),
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

    YA_LOG_INFO("Render", "Geometry arena quantization: worst position error %.3f cm on a mesh %.1f m across",
      m_MaxQuantizeError * 100.0f, m_MaxQuantizeErrorExtent);
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
    // no mesh record needs fixing up.
    buffer.AdoptStorage(grown);
    grown.Destroy(ctx);

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

    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    if (vmaCreateBuffer(ctx.allocator, &stagingInfo, &stagingAllocInfo, &stagingBuffer, &stagingAlloc, nullptr) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create geometry arena staging buffer");
      throw std::runtime_error("Failed to create geometry arena staging buffer");
    }

    void* mapped = nullptr;
    vmaMapMemory(ctx.allocator, stagingAlloc, &mapped);
    size_t cursor = 0;
    for (size_t i = 0; i < count; i++)
    {
      std::memcpy(static_cast<uint8_t*>(mapped) + cursor, copies[i].data, copies[i].size);
      cursor += copies[i].size;
    }
    vmaUnmapMemory(ctx.allocator, stagingAlloc);

    VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();

    cursor = 0;
    for (size_t i = 0; i < count; i++)
    {
      VkBufferCopy region {};
      region.srcOffset = cursor;
      region.dstOffset = copies[i].destinationOffset;
      region.size = copies[i].size;
      vkCmdCopyBuffer(cmd, stagingBuffer, copies[i].destination, 1, &region);
      cursor += copies[i].size;
    }

    ctx.commandBuffer->EndSingleTimeCommands(cmd);
    vmaDestroyBuffer(ctx.allocator, stagingBuffer, stagingAlloc);
  }

  GeometryArenaAllocation GeometryArena::Upload(const RenderContext& ctx,
    const glm::vec3* positions, size_t positionCount,
    const uint32_t* indices, size_t indexCount,
    const QuantizedPositions& quantized)
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
      if (!b_ExhaustionReported)
      {
        b_ExhaustionReported = true;
        YA_LOG_WARN("Render", "Geometry arena position allocation failed, mesh falls back to standalone buffers");
      }
      return result;
    }

    VulkanBuffer& indexBuffer = IndexBuffer(indexType);
    uint32_t indexOffset = AllocateGrowing(ctx, indexBuffer, indexAllocSize, INDEX_USAGE, indexBufferName);
    if (indexOffset == UINT32_MAX)
    {
      m_Positions.Free(positionOffset, positionAllocSize);
      if (!b_ExhaustionReported)
      {
        b_ExhaustionReported = true;
        YA_LOG_WARN("Render", "Geometry arena index allocation failed, mesh falls back to standalone buffers");
      }
      return result;
    }

    if (positionOffset % POSITION_STRIDE != 0 || indexOffset % indexStride != 0)
    {
      YA_LOG_ERROR("Render",
        "Geometry arena produced misaligned offsets: positions %u (stride %u), indices %u (stride %u)",
        positionOffset, POSITION_STRIDE, indexOffset, indexStride);
      m_Positions.Free(positionOffset, positionAllocSize);
      indexBuffer.Free(indexOffset, indexAllocSize);
      return result;
    }

    // Optional third stream: failing to place it only costs the mesh its seat in the
    // quantized shadow path, so nothing allocated above is rolled back for it.
    size_t shadowBytes = quantized.data.size() * sizeof(uint16_t);
    uint32_t shadowOffset = UINT32_MAX;
    uint32_t shadowAllocSize = 0;
    if (quantized.data.size() == positionCount * 4 && shadowBytes <= ARENA_MAX_BYTES)
    {
      shadowAllocSize = RoundUp(uint32_t(shadowBytes), SHADOW_POSITION_STRIDE);
      shadowOffset = AllocateGrowing(ctx, m_ShadowPositions, shadowAllocSize, POSITION_USAGE,
        "shadow positions");

      if (shadowOffset != UINT32_MAX && shadowOffset % SHADOW_POSITION_STRIDE != 0)
      {
        YA_LOG_ERROR("Render",
          "Geometry arena produced a misaligned shadow position offset: %u (stride %u)",
          shadowOffset, SHADOW_POSITION_STRIDE);
        m_ShadowPositions.Free(shadowOffset, shadowAllocSize);
        shadowOffset = UINT32_MAX;
      }

      if (shadowOffset == UINT32_MAX && !b_ShadowExhaustionReported)
      {
        b_ShadowExhaustionReported = true;
        YA_LOG_WARN("Render",
          "Geometry arena shadow position allocation failed, the mesh falls out of the quantized shadow path");
      }
    }

    std::vector<uint16_t> narrowIndices;
    if (narrow)
      NarrowIndices(indices, indexCount, narrowIndices);

    const void* indexData = narrow
      ? static_cast<const void*>(narrowIndices.data())
      : static_cast<const void*>(indices);

    StagedCopy copies[3] {};
    size_t copyCount = 0;
    copies[copyCount++] = { positions, positionBytes, m_Positions.Get(), positionOffset };
    copies[copyCount++] = { indexData, indexBytes, indexBuffer.Get(), indexOffset };
    if (shadowOffset != UINT32_MAX)
      copies[copyCount++] = { quantized.data.data(), shadowBytes, m_ShadowPositions.Get(), shadowOffset };
    StageCopies(ctx, copies, copyCount);

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

    m_PositionHighWater = std::max(m_PositionHighWater, m_Positions.GetAllocatorFrontier());
    uint32_t& indexHighWater = narrow ? m_IndexHighWater16 : m_IndexHighWater32;
    indexHighWater = std::max(indexHighWater, indexBuffer.GetAllocatorFrontier());
    m_IndexBytes += indexBytes;
    m_WideIndexBytes += indexCount * sizeof(uint32_t);

    return result;
  }

  GeometryArenaIndexRange GeometryArena::UploadIndices(const RenderContext& ctx,
    const uint32_t* indices, size_t indexCount, VkIndexType indexType)
  {
    GeometryArenaIndexRange result {};

    if (indices == nullptr || indexCount == 0)
      return result;

    bool narrow = indexType == VK_INDEX_TYPE_UINT16;
    uint32_t indexStride = IndexStride(indexType);

    size_t indexBytes = indexCount * indexStride;
    if (indexBytes > ARENA_MAX_BYTES)
      return result;

    VulkanBuffer& indexBuffer = IndexBuffer(indexType);
    uint32_t indexAllocSize = RoundUp(uint32_t(indexBytes), indexStride);
    uint32_t indexOffset = AllocateGrowing(ctx, indexBuffer, indexAllocSize, INDEX_USAGE,
      narrow ? "16-bit indices" : "32-bit indices");
    if (indexOffset == UINT32_MAX)
      return result;

    if (indexOffset % indexStride != 0)
    {
      YA_LOG_ERROR("Render", "Geometry arena produced a misaligned index offset: %u (stride %u)",
        indexOffset, indexStride);
      indexBuffer.Free(indexOffset, indexAllocSize);
      return result;
    }

    std::vector<uint16_t> narrowIndices;
    if (narrow)
      NarrowIndices(indices, indexCount, narrowIndices);

    const StagedCopy copies[] = {
      { narrow ? static_cast<const void*>(narrowIndices.data()) : static_cast<const void*>(indices),
        indexBytes, indexBuffer.Get(), indexOffset },
    };
    StageCopies(ctx, copies, std::size(copies));

    result.byteOffset = indexOffset;
    result.byteSize = indexAllocSize;
    result.firstIndex = indexOffset / indexStride;
    result.indexCount = uint32_t(indexCount);
    result.indexType = indexType;
    result.resident = true;

    uint32_t& indexHighWater = narrow ? m_IndexHighWater16 : m_IndexHighWater32;
    indexHighWater = std::max(indexHighWater, indexBuffer.GetAllocatorFrontier());
    m_LodIndexBytes += indexBytes;

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
    allocation = {};
  }

  void GeometryArena::FreeIndices(GeometryArenaIndexRange& range)
  {
    if (!range.resident)
      return;

    IndexBuffer(range.indexType).Free(range.byteOffset, range.byteSize);
    m_LodIndexBytes -= size_t(range.indexCount) * IndexStride(range.indexType);
    range = {};
  }
}
