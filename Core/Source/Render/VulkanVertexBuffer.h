#pragma once

#include "Pch.h"
#include "VulkanBuffer.h"

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
    void Destroy(const RenderContext& ctx);

    void Draw(VkCommandBuffer cmd, uint32_t instanceCount = 1);
    void DrawPositionOnly(VkCommandBuffer cmd, uint32_t instanceCount = 1);

    VkBuffer Get() const { return m_VerticesBuffer.Get(); }
    size_t GetIndexCount() const { return m_IndicesCount; }

  private:

    VulkanBuffer m_VerticesBuffer;
    VulkanBuffer m_IndicesBuffer;
    size_t m_IndicesCount {};
    VkDeviceSize m_AttribOffset {};
  };
}
