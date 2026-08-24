#include "VulkanVertexBuffer.h"

#include "RenderContext.h"
#include "Utils/PositionWelder.h"

namespace YAEngine
{
  void VulkanVertexBuffer::Create(const RenderContext& ctx, const void* inputData, size_t vertexCount, uint32_t vertexSize, const std::vector<uint32_t>& indices)
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
        CreateWeldedPositions(ctx, welded.positions, welded.indices);
    }
  }

  void VulkanVertexBuffer::CreateFromSoA(const RenderContext& ctx, const CpuMeshData& cpuData)
  {
    VkDeviceSize vertexSize = cpuData.vertexData.size();
    m_AttribOffset = cpuData.attribOffset;

    m_VerticesBuffer = VulkanBuffer::CreateStaged(ctx, cpuData.vertexData.data(), vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    m_IndicesCount = cpuData.indices.size();
    VkDeviceSize indicesSize = cpuData.indices.size() * sizeof(uint32_t);
    m_IndicesBuffer = VulkanBuffer::CreateStaged(ctx, cpuData.indices.data(), indicesSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    if (!cpuData.weldedPositions.empty())
      CreateWeldedPositions(ctx, cpuData.weldedPositions, cpuData.weldedIndices);
  }

  void VulkanVertexBuffer::CreateWeldedPositions(const RenderContext& ctx,
    const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices)
  {
    if (ctx.geometryArena != nullptr)
    {
      m_ArenaAllocation = ctx.geometryArena->Upload(ctx,
        positions.data(), positions.size(), indices.data(), indices.size());

      if (m_ArenaAllocation.resident)
      {
        m_Arena = ctx.geometryArena;
        return;
      }
    }

    CreateStandalonePositions(ctx, positions, indices);
  }

  void VulkanVertexBuffer::CreateStandalonePositions(const RenderContext& ctx,
    const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices)
  {
    m_PositionIndexCount = indices.size();
    m_PositionIndexOffset = positions.size() * sizeof(glm::vec3);

    // A vec3 stream is 12-byte aligned, which satisfies the 2- and 4-byte index
    // offset alignment Vulkan requires, so no padding is needed between them.
    bool narrow = positions.size() <= 65536;
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

  CpuMeshData VulkanVertexBuffer::PrepareSoA(const std::vector<Vertex>& vertices, std::vector<uint32_t> indices)
  {
    CpuMeshData result;
    result.indices = std::move(indices);
    result.vertexCount = vertices.size();

    if (!vertices.empty())
    {
      glm::vec3 bbMin = vertices[0].position;
      glm::vec3 bbMax = vertices[0].position;
      for (size_t i = 1; i < vertices.size(); i++)
      {
        bbMin = glm::min(bbMin, vertices[i].position);
        bbMax = glm::max(bbMax, vertices[i].position);
      }
      result.minBB = bbMin;
      result.maxBB = bbMax;
    }

    size_t posSize = vertices.size() * sizeof(glm::vec3);
    size_t attribSize = vertices.size() * sizeof(VertexAttribs);
    result.attribOffset = posSize;
    result.vertexData.resize(posSize + attribSize);

    auto* dstPos = reinterpret_cast<glm::vec3*>(result.vertexData.data());
    auto* dstAttrib = reinterpret_cast<VertexAttribs*>(result.vertexData.data() + posSize);

    for (size_t i = 0; i < vertices.size(); i++)
    {
      dstPos[i] = vertices[i].position;
      dstAttrib[i] = { vertices[i].tex, vertices[i].normal, vertices[i].tangent };
    }

    auto welded = PositionWelder::Weld(dstPos, vertices.size(), result.indices,
      PositionWelder::KEEP_ALWAYS_RATIO);
    if (welded.worthwhile)
    {
      result.weldedPositions = std::move(welded.positions);
      result.weldedIndices = std::move(welded.indices);
    }

    return result;
  }

  void VulkanVertexBuffer::Destroy(const RenderContext& ctx)
  {
    m_VerticesBuffer.Destroy(ctx);
    m_IndicesBuffer.Destroy(ctx);

    if (m_Arena != nullptr)
    {
      m_Arena->Free(m_ArenaAllocation);
      m_Arena = nullptr;
    }

    if (b_HasStandalonePositions)
    {
      m_PositionsBuffer.Destroy(ctx);
      b_HasStandalonePositions = false;
    }
  }

  void VulkanVertexBuffer::Draw(VkCommandBuffer cmd, uint32_t instanceCount)
  {
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

  void VulkanVertexBuffer::DrawPositionOnly(VkCommandBuffer cmd, uint32_t instanceCount)
  {
    if (m_ArenaAllocation.resident)
    {
      VkBuffer positions = m_Arena->GetPositionBuffer();
      VkDeviceSize offset = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &positions, &offset);
      vkCmdBindIndexBuffer(cmd, m_Arena->GetIndexBuffer(m_ArenaAllocation.indexType), 0,
        m_ArenaAllocation.indexType);
      vkCmdDrawIndexed(cmd, m_ArenaAllocation.indexCount, instanceCount,
        m_ArenaAllocation.firstIndex, static_cast<int32_t>(m_ArenaAllocation.vertexOffset), 0);
      return;
    }

    VkBuffer buf = b_HasStandalonePositions ? m_PositionsBuffer.Get() : m_VerticesBuffer.Get();
    VkDeviceSize offset = 0;

    if (b_HasStandalonePositions)
      vkCmdBindIndexBuffer(cmd, buf, m_PositionIndexOffset, m_PositionIndexType);
    else
      vkCmdBindIndexBuffer(cmd, m_IndicesBuffer.Get(), 0, VK_INDEX_TYPE_UINT32);

    vkCmdBindVertexBuffers(cmd, 0, 1, &buf, &offset);

    size_t indexCount = b_HasStandalonePositions ? m_PositionIndexCount : m_IndicesCount;
    vkCmdDrawIndexed(cmd, static_cast<uint32_t>(indexCount), instanceCount, 0, 0, 0);
  }
}
