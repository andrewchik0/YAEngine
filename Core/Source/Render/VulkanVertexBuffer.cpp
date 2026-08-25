#include "VulkanVertexBuffer.h"

#include "RenderContext.h"
#include "Utils/Log.h"
#include "Utils/PositionWelder.h"

namespace YAEngine
{
  void VulkanVertexBuffer::Create(const RenderContext& ctx, const void* inputData, size_t vertexCount,
    uint32_t vertexSize, const std::vector<uint32_t>& indices, bool generateShadowLods)
  {
    VkDeviceSize totalSize = vertexCount * vertexSize;

    // Reorganize Vertex data from AoS to SoA: [positions] [attribs]
    if (vertexSize == sizeof(Vertex))
    {
      VkDeviceSize posSize = vertexCount * sizeof(glm::vec3);
      VkDeviceSize attribSize = vertexCount * sizeof(VertexAttribs);
      m_AttribOffset = posSize;

      std::vector<uint8_t> soaData(posSize + attribSize);

      auto* src = static_cast<const Vertex*>(inputData);
      auto* dstPos = reinterpret_cast<glm::vec3*>(soaData.data());
      auto* dstAttrib = reinterpret_cast<VertexAttribs*>(soaData.data() + posSize);

      for (size_t i = 0; i < vertexCount; i++)
      {
        dstPos[i] = src[i].position;
        dstAttrib[i] = { src[i].tex, src[i].normal, src[i].tangent };
      }

      m_VerticesBuffer = VulkanBuffer::CreateStaged(ctx, soaData.data(), totalSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    }
    else
    {
      m_AttribOffset = 0;
      m_VerticesBuffer = VulkanBuffer::CreateStaged(ctx, inputData, totalSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    }

    m_IndicesCount = indices.size();
    VkDeviceSize indicesSize = indices.size() * sizeof(uint32_t);
    m_IndicesBuffer = VulkanBuffer::CreateStaged(ctx, indices.data(), indicesSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    // Position is the leading member of every vertex format the engine feeds here.
    if (vertexSize >= sizeof(glm::vec3))
    {
      std::vector<glm::vec3> positions(vertexCount);
      auto* bytes = static_cast<const uint8_t*>(inputData);
      for (size_t i = 0; i < vertexCount; i++)
        std::memcpy(&positions[i], bytes + i * vertexSize, sizeof(glm::vec3));

      auto welded = PositionWelder::Weld(positions.data(), vertexCount, indices,
        PositionWelder::KEEP_ALWAYS_RATIO);
      if (welded.worthwhile)
      {
        MeshLodLevels lods;
        if (generateShadowLods)
          lods = MeshSimplifier::Build(welded.positions.data(), welded.positions.size(), welded.indices);

        // Nothing can read a UNORM16 vertex stream without the format, so on a device
        // that lacks it the packing is not worth running either.
        QuantizedPositions quantized;
        if (ctx.unorm16VertexSupported)
          quantized = PositionQuantizer::Quantize(welded.positions.data(), welded.positions.size());

        CreateWeldedPositions(ctx, welded.positions, welded.indices, &lods, quantized);
      }
    }
  }

  void VulkanVertexBuffer::CreateWeldedPositions(const RenderContext& ctx,
    const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices,
    const MeshLodLevels* lods, const QuantizedPositions& quantized)
  {
    if (ctx.geometryArena != nullptr)
    {
      // The LOD index ranges go in with the mesh: they index the same vertices, and a
      // call of their own would cost a queue submit plus a fence wait per level.
      m_ArenaAllocation = ctx.geometryArena->Upload(ctx,
        positions.data(), positions.size(), indices.data(), indices.size(), quantized,
        lods, m_LodRanges, std::size(m_LodRanges));

      if (m_ArenaAllocation.resident)
      {
        m_Arena = ctx.geometryArena;
        return;
      }
    }

    CreateStandalonePositions(ctx, positions, indices);
  }

  MeshLodRange VulkanVertexBuffer::GetLodRange(uint32_t lodLevel) const
  {
    for (uint32_t level = std::min(lodLevel, uint32_t(std::size(m_LodRanges))); level > 0; level--)
    {
      const auto& range = m_LodRanges[level - 1];
      if (range.resident)
        return { range.firstIndex, range.indexCount };
    }

    if (m_ArenaAllocation.resident)
      return { m_ArenaAllocation.firstIndex, m_ArenaAllocation.indexCount };

    if (b_HasStandalonePositions)
      return { 0, uint32_t(m_PositionIndexCount) };

    return { 0, uint32_t(m_IndicesCount) };
  }

  void VulkanVertexBuffer::CreateStandalonePositions(const RenderContext& ctx,
    const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices)
  {
    m_PositionIndexCount = indices.size();
    m_PositionIndexOffset = positions.size() * sizeof(glm::vec3);

    uint32_t maxIndex = 0;
    for (uint32_t index : indices)
      maxIndex = std::max(maxIndex, index);

    // The vertex limit already implies every index fits 16 bits, but the width is
    // decided by the indices themselves: a stream that disagrees with its own vertex
    // count would otherwise truncate into garbage triangles without a word.
    bool fitsVertexLimit = positions.size() <= GeometryArena::NARROW_INDEX_VERTEX_LIMIT;
    bool narrow = fitsVertexLimit && maxIndex <= UINT16_MAX;

    if (fitsVertexLimit && maxIndex > UINT16_MAX)
    {
      YA_LOG_WARN("Render",
        "Mesh index %u is out of range for its %zu vertices, the standalone positions keep 32-bit indices",
        maxIndex, positions.size());
    }

    // A vec3 stream is 12-byte aligned, which satisfies the 2- and 4-byte index
    // offset alignment Vulkan requires, so no padding is needed between them.
    m_PositionIndexType = narrow ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    size_t indexSize = narrow ? sizeof(uint16_t) : sizeof(uint32_t);

    std::vector<uint8_t> blob(m_PositionIndexOffset + indices.size() * indexSize);
    std::memcpy(blob.data(), positions.data(), m_PositionIndexOffset);

    if (narrow)
    {
      auto* dst = reinterpret_cast<uint16_t*>(blob.data() + m_PositionIndexOffset);
      for (size_t i = 0; i < indices.size(); i++)
        dst[i] = static_cast<uint16_t>(indices[i]);
    }
    else
    {
      std::memcpy(blob.data() + m_PositionIndexOffset, indices.data(), indices.size() * indexSize);
    }

    m_PositionsBuffer = VulkanBuffer::CreateStaged(ctx, blob.data(), blob.size(),
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    b_HasStandalonePositions = true;
  }

  void VulkanVertexBuffer::Destroy(const RenderContext& ctx)
  {
    m_VerticesBuffer.Destroy(ctx);
    m_IndicesBuffer.Destroy(ctx);

    if (m_Arena != nullptr)
    {
      for (auto& range : m_LodRanges)
        m_Arena->FreeIndices(range);

      m_Arena->Free(m_ArenaAllocation);
      m_Arena = nullptr;
    }

    if (b_HasStandalonePositions)
    {
      m_PositionsBuffer.Destroy(ctx);
      b_HasStandalonePositions = false;
    }
  }

  void VulkanVertexBuffer::BindPositionStream(VkCommandBuffer cmd, MeshBindCache* bindCache,
    VkBuffer vertices, VkBuffer indices, VkDeviceSize indexOffset, VkIndexType indexType)
  {
    if (bindCache != nullptr
      && bindCache->vertices == vertices
      && bindCache->indices == indices
      && bindCache->indexOffset == indexOffset
      && bindCache->indexType == indexType)
    {
      return;
    }

    VkDeviceSize vertexOffset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertices, &vertexOffset);
    vkCmdBindIndexBuffer(cmd, indices, indexOffset, indexType);

    if (bindCache != nullptr)
    {
      bindCache->vertices = vertices;
      bindCache->indices = indices;
      bindCache->indexOffset = indexOffset;
      bindCache->indexType = indexType;
    }
  }

  void VulkanVertexBuffer::Draw(VkCommandBuffer cmd, uint32_t instanceCount, MeshBindCache* bindCache)
  {
    // The interleaved stream binds its own buffers over whatever the position-only
    // draws left bound, and it uses two vertex bindings where they use one.
    if (bindCache != nullptr)
      bindCache->Clear();

    vkCmdBindIndexBuffer(cmd, m_IndicesBuffer.Get(), 0, VK_INDEX_TYPE_UINT32);

    if (m_AttribOffset > 0)
    {
      VkBuffer bufs[] = { m_VerticesBuffer.Get(), m_VerticesBuffer.Get() };
      VkDeviceSize offsets[] = { 0, m_AttribOffset };
      vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
    }
    else
    {
      VkBuffer buf = m_VerticesBuffer.Get();
      VkDeviceSize offset = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &buf, &offset);
    }

    vkCmdDrawIndexed(cmd, static_cast<uint32_t>(m_IndicesCount), instanceCount, 0, 0, 0);
  }

  void VulkanVertexBuffer::DrawPositionOnly(VkCommandBuffer cmd, uint32_t instanceCount,
    uint32_t lodLevel, MeshBindCache* bindCache)
  {
    if (m_ArenaAllocation.resident)
    {
      MeshLodRange lod = GetLodRange(lodLevel);

      // Queried per draw, never cached in the mesh: arena growth swaps the handles.
      // The cache above compares the handles it actually bound, so a swap simply
      // fails the comparison and rebinds.
      BindPositionStream(cmd, bindCache, m_Arena->GetPositionBuffer(),
        m_Arena->GetIndexBuffer(m_ArenaAllocation.indexType), 0, m_ArenaAllocation.indexType);

      vkCmdDrawIndexed(cmd, lod.indexCount, instanceCount,
        lod.firstIndex, static_cast<int32_t>(m_ArenaAllocation.vertexOffset), 0);
      return;
    }

    VkBuffer buf = b_HasStandalonePositions ? m_PositionsBuffer.Get() : m_VerticesBuffer.Get();

    if (b_HasStandalonePositions)
      BindPositionStream(cmd, bindCache, buf, buf, m_PositionIndexOffset, m_PositionIndexType);
    else
      BindPositionStream(cmd, bindCache, buf, m_IndicesBuffer.Get(), 0, VK_INDEX_TYPE_UINT32);

    size_t indexCount = b_HasStandalonePositions ? m_PositionIndexCount : m_IndicesCount;
    vkCmdDrawIndexed(cmd, static_cast<uint32_t>(indexCount), instanceCount, 0, 0, 0);
  }
}
