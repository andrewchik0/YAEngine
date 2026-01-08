#pragma once

#include "Pch.h"
#include "VulkanCommandBuffer.h"

namespace YAEngine
{
  struct Vertex
  {
    glm::vec3 position;
    glm::vec2 tex;
    glm::vec3 normal;
    glm::vec4 tangent;
  };

  class VulkanVertexBuffer
  {
  public:

    static void InitVertexBuffers(VkDevice device, VkQueue queue, VulkanCommandBuffer& commandBuffer, VmaAllocator allocator);

    void Create(void* inputData, size_t vertexCount, uint32_t vertexSize, const std::vector<uint32_t>& indices);
    void Destroy();

    void Draw(VkCommandBuffer cmd);

    VkBuffer Get()
    {
      return m_VerticesBuffer;
    }

  private:

    void CreateIndicesBuffer(const std::vector<uint32_t>& indices);

    VkBuffer m_VerticesBuffer {};
    VmaAllocation m_VerticesAlloc {};
    VkBuffer m_IndicesBuffer {};
    VmaAllocation m_IndicesAlloc {};
    size_t m_VerticesCount {};
    size_t m_IndicesCount {};

    static VkQueue s_GraphicsQueue;
    static VulkanCommandBuffer* s_GraphicsCommandBuffer;
    static VmaAllocator s_GraphicsAllocator;
    static VkDevice s_Device;
  };
}
