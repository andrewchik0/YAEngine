#include "VulkanVertexBuffer.h"

#include "VulkanCommandBuffer.h"

namespace YAEngine
{
  VkQueue VulkanVertexBuffer::s_GraphicsQueue;
  VulkanCommandBuffer* VulkanVertexBuffer::s_GraphicsCommandBuffer;
  VmaAllocator VulkanVertexBuffer::s_GraphicsAllocator;
  VkDevice VulkanVertexBuffer::s_Device;

  void VulkanVertexBuffer::Create(void* inputData, size_t vertexCount, uint32_t vertexSize, const std::vector<uint32_t>& indices)
  {
    m_VerticesCount = vertexCount;
    VkDeviceSize bufferSize = vertexCount * vertexSize;

    VkBufferCreateInfo stagingBufferInfo{};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = vertexCount * vertexSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocation stagingAlloc;
    VkBuffer stagingBuffer;
    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    if (vmaCreateBuffer(s_GraphicsAllocator, &stagingBufferInfo, &stagingAllocInfo, &stagingBuffer, &stagingAlloc, nullptr) != VK_SUCCESS)
    {
      throw std::exception("Failed to create vertex staging buffer");
    }

    void* data;
    vmaMapMemory(s_GraphicsAllocator, stagingAlloc, &data);
    memcpy(data, inputData, (size_t)bufferSize);
    vmaUnmapMemory(s_GraphicsAllocator, stagingAlloc);

    VkBufferCreateInfo vertexBufferInfo{};
    vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vertexBufferInfo.size = bufferSize;
    vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo vertexAllocInfo{};
    vertexAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(s_GraphicsAllocator, &vertexBufferInfo, &vertexAllocInfo, &m_VerticesBuffer, &m_VerticesAlloc, nullptr) != VK_SUCCESS)
    {
      throw std::exception("Failed to create vertex buffer");
    }

    VkCommandBuffer cmd = s_GraphicsCommandBuffer->BeginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;

    vkCmdCopyBuffer(cmd, stagingBuffer, m_VerticesBuffer, 1, &copyRegion);

    s_GraphicsCommandBuffer->EndSingleTimeCommands(cmd);
    vmaDestroyBuffer(s_GraphicsAllocator, stagingBuffer, stagingAlloc);

    CreateIndicesBuffer(indices);
  }

  void VulkanVertexBuffer::InitVertexBuffers(VkDevice device, VkQueue queue, VulkanCommandBuffer& commandBuffer, VmaAllocator allocator)
  {
    s_Device = device;
    s_GraphicsAllocator = allocator;
    s_GraphicsQueue = queue;
    s_GraphicsCommandBuffer = &commandBuffer;
  }

  void VulkanVertexBuffer::Destroy()
  {
    vkDeviceWaitIdle(s_Device);
    vmaDestroyBuffer(s_GraphicsAllocator, m_VerticesBuffer, m_VerticesAlloc);
    vmaDestroyBuffer(s_GraphicsAllocator, m_IndicesBuffer, m_IndicesAlloc);
  }

  void VulkanVertexBuffer::Draw(VkCommandBuffer cmd)
  {
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindIndexBuffer(cmd, m_IndicesBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_VerticesBuffer, offsets);
    vkCmdDrawIndexed(cmd, static_cast<uint32_t>(m_IndicesCount), 1, 0, 0, 0);
  }

  void VulkanVertexBuffer::CreateIndicesBuffer(const std::vector<uint32_t>& indices)
  {
    m_IndicesCount = indices.size();
    VkDeviceSize bufferSize = indices.size() * sizeof(uint32_t);

    VkBufferCreateInfo stagingBufferInfo{};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = bufferSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocation stagingAlloc;
    VkBuffer stagingBuffer;
    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    if (vmaCreateBuffer(s_GraphicsAllocator, &stagingBufferInfo, &stagingAllocInfo, &stagingBuffer, &stagingAlloc, nullptr) != VK_SUCCESS)
    {
      throw std::exception("Failed to create indices staging buffer");
    }

    void* data;
    vmaMapMemory(s_GraphicsAllocator, stagingAlloc, &data);
    memcpy(data, indices.data(), (size_t)bufferSize);
    vmaUnmapMemory(s_GraphicsAllocator, stagingAlloc);

    VkBufferCreateInfo vertexBufferInfo{};
    vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vertexBufferInfo.size = bufferSize;
    vertexBufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo vertexAllocInfo{};
    vertexAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(s_GraphicsAllocator, &vertexBufferInfo, &vertexAllocInfo, &m_IndicesBuffer, &m_IndicesAlloc, nullptr) != VK_SUCCESS)
    {
      throw std::exception("Failed to create indices buffer");
    }

    VkCommandBuffer cmd = s_GraphicsCommandBuffer->BeginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;

    vkCmdCopyBuffer(cmd, stagingBuffer, m_IndicesBuffer, 1, &copyRegion);

    s_GraphicsCommandBuffer->EndSingleTimeCommands(cmd);
    vmaDestroyBuffer(s_GraphicsAllocator, stagingBuffer, stagingAlloc);
  }
}
