#pragma once

#include "Pch.h"
#include "VulkanBuffer.h"
#include "Assets/CpuResourceData.h"

namespace YAEngine
{
  struct RenderContext;

  struct Vertex
  {
    glm::vec3 position;
    glm::vec2 tex;
    glm::vec3 normal;
    glm::vec4 tangent;
  };

  struct VertexAttribs
  {
    glm::vec2 tex;
    glm::vec3 normal;
    glm::vec4 tangent;
  };

  class VulkanVertexBuffer
  {
  public:

    void Create(const RenderContext& ctx, const void* inputData, size_t vertexCount, uint32_t vertexSize, const std::vector<uint32_t>& indices);

    static CpuMeshData PrepareSoA(const std::vector<Vertex>& vertices, std::vector<uint32_t> indices);

    void CreateFromSoA(const RenderContext& ctx, const CpuMeshData& cpuData);
    void Destroy(const RenderContext& ctx);

    void Draw(VkCommandBuffer cmd, uint32_t instanceCount = 1);
    void DrawPositionOnly(VkCommandBuffer cmd, uint32_t instanceCount = 1);

    VkBuffer Get() const { return m_VerticesBuffer.Get(); }
    size_t GetIndexCount() const { return m_IndicesCount; }

  private:

    void CreateWeldedPositions(const RenderContext& ctx,
      const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices);

    VulkanBuffer m_VerticesBuffer;
    VulkanBuffer m_IndicesBuffer;
    size_t m_IndicesCount {};
    VkDeviceSize m_AttribOffset {};

    // Position-only stream with duplicate positions removed. Depth prepass and
    // shadow passes fetch this instead of the interleaved stream, which carries
    // one vertex per attribute combination and reuses none of them.
    // Positions and indices share one allocation: every staged upload costs a
    // queue submit plus a fence wait, and meshes are uploaded one by one.
    VulkanBuffer m_PositionsBuffer;
    VkDeviceSize m_PositionIndexOffset {};
    size_t m_PositionIndexCount {};
    VkIndexType m_PositionIndexType = VK_INDEX_TYPE_UINT32;
    bool b_HasWeldedPositions = false;
  };
}
