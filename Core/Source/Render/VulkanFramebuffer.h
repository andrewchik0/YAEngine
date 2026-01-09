#pragma once

namespace YAEngine
{
  class VulkanFramebuffer
  {
  public:

    void Init(VkDevice device, VmaAllocator allocator, VkRenderPass renderPass, uint32_t width, uint32_t height, VkFormat format);
    void Destroy();

    void Begin(VkCommandBuffer cmd);
    void End(VkCommandBuffer cmd);

    void Recreate(VkRenderPass renderPass, uint32_t width, uint32_t height);

    VkFramebuffer& Get()
    {
      return m_Framebuffer;
    }

    VkImageView& GetImageView()
    {
      return m_ImageView;
    }

    VkImageLayout& GetLayout()
    {
      return m_ImageLayout;
    }

    VkSampler& GetSampler()
    {
      return m_Sampler;
    }

  private:

    VkFramebuffer m_Framebuffer {};

    VkImage m_Image {};
    VkImageView m_ImageView {};
    VkImageLayout m_ImageLayout {};
    VkSampler m_Sampler {};
    VmaAllocation m_ImageAllocation {};

    VkImageView m_MultisampleImageView {};
    VkImage m_MultisampleImage {};
    VmaAllocation m_MultisampleImageAllocation {};

    VkImageView m_DepthImageView {};
    VkImage m_DepthImage {};
    VmaAllocation m_DepthImageAllocation {};

    VkRenderPass m_RenderPass {};

    VkFormat m_Format {};
    VmaAllocator m_Allocator {};
    VkDevice m_Device {};
  };
}