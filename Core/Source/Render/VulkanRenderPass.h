#pragma once

namespace YAEngine
{
  class VulkanRenderPass
  {
  public:

    void Init(VkDevice device, VkFormat swapChainImageFormat, VmaAllocator allocator, VkImageLayout finalImageLayout, bool multisampleBuffer);
    void Destroy();
    void Recreate();

    void Begin(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, VkExtent2D swapChainExtent);
    void End(VkCommandBuffer commandBuffer);

    VkRenderPass& Get()
    {
      return m_RenderPass;
    }

  private:

    VkRenderPass m_RenderPass {};
    VkImageLayout m_ImageLayout {};
    bool m_Multisample {};

    VkDevice m_Device {};
    VkFormat m_SwapChainImageFormat {};
    VmaAllocator m_Allocator {};
  };
}