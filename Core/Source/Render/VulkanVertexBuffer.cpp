#include "VulkanVertexBuffer.h"

#include "RenderContext.h"

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
  }

  void VulkanVertexBuffer::CreateFromSoA(const RenderContext& ctx, const CpuMeshData& cpuData)
  {
    VkDeviceSize vertexSize = cpuData.vertexData.size();
    m_AttribOffset = cpuData.attribOffset;

    m_VerticesBuffer = VulkanBuffer::CreateStaged(ctx, cpuData.vertexData.data(), vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    m_IndicesCount = cpuData.indices.size();
    VkDeviceSize indicesSize = cpuData.indices.size() * sizeof(uint32_t);
    m_IndicesBuffer = VulkanBuffer::CreateStaged(ctx, cpuData.indices.data(), indicesSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
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

    return result;
  }

  void VulkanVertexBuffer::Destroy(const RenderContext& ctx)
  {
    m_VerticesBuffer.Destroy(ctx);
    m_IndicesBuffer.Destroy(ctx);
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
    vkCmdBindIndexBuffer(cmd, m_IndicesBuffer.Get(), 0, VK_INDEX_TYPE_UINT32);

    VkBuffer buf = m_VerticesBuffer.Get();
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &buf, &offset);

    vkCmdDrawIndexed(cmd, static_cast<uint32_t>(m_IndicesCount), instanceCount, 0, 0, 0);
  }
}
