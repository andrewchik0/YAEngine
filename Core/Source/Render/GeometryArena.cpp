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
    m_Indices = VulkanBuffer::CreateGpuOnly(ctx, INDEX_INITIAL_BYTES, INDEX_USAGE);

    YA_LOG_INFO("Render", "Geometry arena created: positions %llu MB, indices %llu MB",
      (unsigned long long)(POSITION_INITIAL_BYTES / (1024 * 1024)),
      (unsigned long long)(INDEX_INITIAL_BYTES / (1024 * 1024)));
  }

  void GeometryArena::Destroy(const RenderContext& ctx)
  {
    LogUsage("shutdown");
    m_Positions.Destroy(ctx);
    m_Indices.Destroy(ctx);
  }

  uint32_t GeometryArena::GetPositionUsedBytes() const
  {
    return m_Positions.GetAllocatorFrontier() - m_Positions.GetAllocatorFreeBytes();
  }

  uint32_t GeometryArena::GetIndexUsedBytes() const
  {
    return m_Indices.GetAllocatorFrontier() - m_Indices.GetAllocatorFreeBytes();
  }

  void GeometryArena::LogUsage(const char* reason) const
  {
    YA_LOG_INFO("Render",
      "Geometry arena (%s): positions %u/%llu KB used, high water %u KB, %zu free blocks; "
      "indices %u/%llu KB used, high water %u KB, %zu free blocks",
      reason,
      GetPositionUsedBytes() / 1024, (unsigned long long)(GetPositionCapacityBytes() / 1024),
      m_PositionHighWater / 1024, GetPositionFreeBlockCount(),
      GetIndexUsedBytes() / 1024, (unsigned long long)(GetIndexCapacityBytes() / 1024),
      m_IndexHighWater / 1024, GetIndexFreeBlockCount());

    YA_LOG_INFO("Render", "Geometry arena index width cost: %llu KB uploaded as 32-bit, %llu KB more than 16-bit would have taken",
      (unsigned long long)(m_IndexBytes / 1024),
      (unsigned long long)(GetIndexWideningBytes() / 1024));

    YA_LOG_INFO("Render", "Geometry arena shadow LOD cost: %llu KB of index memory",
      (unsigned long long)(m_LodIndexBytes / 1024));
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
    const uint32_t* indices, size_t indexCount)
  {
    GeometryArenaAllocation result {};

    if (positions == nullptr || positionCount == 0 || indices == nullptr || indexCount == 0)
      return result;

    size_t positionBytes = positionCount * POSITION_STRIDE;
    size_t indexBytes = indexCount * INDEX_STRIDE;

    if (positionBytes > ARENA_MAX_BYTES || indexBytes > ARENA_MAX_BYTES)
    {
      YA_LOG_ERROR("Render", "Mesh too large for the geometry arena: %zu position bytes, %zu index bytes",
        positionBytes, indexBytes);
      return result;
    }

    // Rounding every allocation keeps every offset a multiple of the stride, so the
    // byte offset -> element offset conversions below are always exact.
    uint32_t positionAllocSize = RoundUp(uint32_t(positionBytes), POSITION_STRIDE);
    uint32_t indexAllocSize = RoundUp(uint32_t(indexBytes), INDEX_STRIDE);

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

    uint32_t indexOffset = AllocateGrowing(ctx, m_Indices, indexAllocSize, INDEX_USAGE, "indices");
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

    if (positionOffset % POSITION_STRIDE != 0 || indexOffset % INDEX_STRIDE != 0)
    {
      YA_LOG_ERROR("Render",
        "Geometry arena produced misaligned offsets: positions %u (stride %u), indices %u (stride %u)",
        positionOffset, POSITION_STRIDE, indexOffset, INDEX_STRIDE);
      m_Positions.Free(positionOffset, positionAllocSize);
      m_Indices.Free(indexOffset, indexAllocSize);
      return result;
    }

    const StagedCopy copies[] = {
      { positions, positionBytes, m_Positions.Get(), positionOffset },
      { indices, indexBytes, m_Indices.Get(), indexOffset },
    };
    StageCopies(ctx, copies, std::size(copies));

    result.positionByteOffset = positionOffset;
    result.positionByteSize = positionAllocSize;
    result.indexByteOffset = indexOffset;
    result.indexByteSize = indexAllocSize;
    result.vertexOffset = positionOffset / POSITION_STRIDE;
    result.firstIndex = indexOffset / INDEX_STRIDE;
    result.indexCount = uint32_t(indexCount);
    result.indexType = VK_INDEX_TYPE_UINT32;
    result.resident = true;

    m_PositionHighWater = std::max(m_PositionHighWater, m_Positions.GetAllocatorFrontier());
    m_IndexHighWater = std::max(m_IndexHighWater, m_Indices.GetAllocatorFrontier());
    m_IndexBytes += indexBytes;
    m_NarrowIndexBytes += indexCount * (positionCount <= 65536 ? sizeof(uint16_t) : sizeof(uint32_t));

    return result;
  }

  GeometryArenaIndexRange GeometryArena::UploadIndices(const RenderContext& ctx,
    const uint32_t* indices, size_t indexCount)
  {
    GeometryArenaIndexRange result {};

    if (indices == nullptr || indexCount == 0)
      return result;

    size_t indexBytes = indexCount * INDEX_STRIDE;
    if (indexBytes > ARENA_MAX_BYTES)
      return result;

    uint32_t indexAllocSize = RoundUp(uint32_t(indexBytes), INDEX_STRIDE);
    uint32_t indexOffset = AllocateGrowing(ctx, m_Indices, indexAllocSize, INDEX_USAGE, "indices");
    if (indexOffset == UINT32_MAX)
      return result;

    if (indexOffset % INDEX_STRIDE != 0)
    {
      YA_LOG_ERROR("Render", "Geometry arena produced a misaligned index offset: %u (stride %u)",
        indexOffset, INDEX_STRIDE);
      m_Indices.Free(indexOffset, indexAllocSize);
      return result;
    }

    const StagedCopy copies[] = {
      { indices, indexBytes, m_Indices.Get(), indexOffset },
    };
    StageCopies(ctx, copies, std::size(copies));

    result.byteOffset = indexOffset;
    result.byteSize = indexAllocSize;
    result.firstIndex = indexOffset / INDEX_STRIDE;
    result.indexCount = uint32_t(indexCount);
    result.resident = true;

    m_IndexHighWater = std::max(m_IndexHighWater, m_Indices.GetAllocatorFrontier());
    m_LodIndexBytes += indexBytes;

    return result;
  }

  void GeometryArena::Free(GeometryArenaAllocation& allocation)
  {
    if (!allocation.resident)
      return;

    m_Positions.Free(allocation.positionByteOffset, allocation.positionByteSize);
    m_Indices.Free(allocation.indexByteOffset, allocation.indexByteSize);
    allocation = {};
  }

  void GeometryArena::FreeIndices(GeometryArenaIndexRange& range)
  {
    if (!range.resident)
      return;

    m_Indices.Free(range.byteOffset, range.byteSize);
    m_LodIndexBytes -= size_t(range.indexCount) * INDEX_STRIDE;
    range = {};
  }
}
