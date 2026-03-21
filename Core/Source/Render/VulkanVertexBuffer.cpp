#include "VulkanVertexBuffer.h"

#include "RenderContext.h"

namespace YAEngine
{
  void VulkanVertexBuffer::Create(const RenderContext& ctx, const void* inputData, size_t vertexCount, uint32_t vertexSize, const std::vector<uint32_t>& indices)
  {
    VkDeviceSize vertexSize_ = vertexCount * vertexSize;
    m_VerticesBuffer = VulkanBuffer::CreateStaged(ctx, inputData, vertexSize_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    m_IndicesCount = indices.size();
    VkDeviceSize indicesSize = indices.size() * sizeof(uint32_t);
    m_IndicesBuffer = VulkanBuffer::CreateStaged(ctx, indices.data(), indicesSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
  }

  void VulkanVertexBuffer::Destroy(const RenderContext& ctx)
  {
    vkDeviceWaitIdle(ctx.device);
    m_VerticesBuffer.Destroy(ctx);
    m_IndicesBuffer.Destroy(ctx);
  }

  void VulkanVertexBuffer::Draw(VkCommandBuffer cmd, uint32_t instanceCount)
  {
    VkBuffer buf = m_VerticesBuffer.Get();
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindIndexBuffer(cmd, m_IndicesBuffer.Get(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindVertexBuffers(cmd, 0, 1, &buf, offsets);
    vkCmdDrawIndexed(cmd, static_cast<uint32_t>(m_IndicesCount), instanceCount, 0, 0, 0);
  }
}
