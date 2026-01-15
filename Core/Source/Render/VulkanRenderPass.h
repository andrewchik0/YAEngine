#pragma once

namespace YAEngine
{
  class VulkanRenderPass
  {
  public:

    void Init(VkDevice device, VkFormat swapChainImageFormat, VmaAllocator allocator, VkImageLayout finalImageLayout, bool clear = true, bool secondaryBuffer = false);
    void Destroy();
    void Recreate(bool clear = true, bool secondaryBuffer = false);

    void Begin(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, VkExtent2D swapChainExtent);
    void End(VkCommandBuffer commandBuffer);

    VkRenderPass& Get()
    {
      return m_RenderPass;
    }

  private:

    bool b_SecondaryBuffer = false;

    VkRenderPass m_RenderPass {};
    VkImageLayout m_ImageLayout {};

    VkDevice m_Device {};
    VkFormat m_SwapChainImageFormat {};
    VmaAllocator m_Allocator {};
  };
}