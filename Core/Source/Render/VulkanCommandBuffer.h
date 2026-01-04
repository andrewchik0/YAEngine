#pragma once

namespace YAEngine
{
  class VulkanCommandBuffer
  {
  public:

    void Init(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t maxFramesInFlight);
    void Destroy();

    void SetGraphicsQueue(VkQueue queue);
    void Set(size_t currentFrameIndex);
    void End(size_t currentFrameIndex);

    VkCommandBuffer GetCurrentBuffer()
    {
      return m_CommandBuffers[m_CurrentFrameIndex];
    }

    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

  private:

    VkCommandPool m_CommandPool {};
    std::vector<VkCommandBuffer> m_CommandBuffers;

    uint32_t m_CurrentFrameIndex {};

    VkDevice m_Device {};
    VkQueue m_Queue {};
  };
}