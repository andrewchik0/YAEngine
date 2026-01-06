#pragma once

namespace YAEngine
{
  class VulkanRenderPass
  {
  public:

    void Init(VkDevice device, VkFormat swapChainImageFormat, VmaAllocator allocator, uint32_t width, uint32_t height);
    void Destroy();
    void Recreate(uint32_t width, uint32_t height);

    void Begin(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, VkExtent2D swapChainExtent);
    void End(VkCommandBuffer commandBuffer);

    VkRenderPass& Get()
    {
      return m_RenderPass;
    }

    VkImageView GetDepthView()
    {
      return m_DepthImageView;
    }

    VkImageView GetMultisampleView()
    {
      return m_MultisampleImageView;
    }

  private:

    VkRenderPass m_RenderPass {};
    VkImage m_DepthImage {};
    VmaAllocation m_DepthImageAllocation {};
    VkImageView m_DepthImageView {};

    VkImage m_MultisampleImage {};
    VmaAllocation m_MultisampleImageAllocation {};
    VkImageView m_MultisampleImageView {};

    VkDevice m_Device {};
    VkFormat m_SwapChainImageFormat {};
    VmaAllocator m_Allocator {};
  };
}