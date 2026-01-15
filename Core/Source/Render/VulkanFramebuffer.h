#pragma once

namespace YAEngine
{
  class VulkanFramebuffer
  {
  public:

    void Init(VkDevice device, VmaAllocator allocator, VkRenderPass renderPass, uint32_t width, uint32_t height, VkFormat format, bool secondaryImageBuffer = false);
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

    VkImage& GetImage()
    {
      return m_Image;
    }

    VkImageView& GetDepthImageView()
    {
      return m_DepthImageView;
    }

    VkImageLayout& GetDepthLayout()
    {
      return m_DepthImageLayout;
    }

    VkSampler& GetDepthSampler()
    {
      return m_DepthSampler;
    }

    VkImage& GetDepthImage()
    {
      return m_DepthImage;
    }

    VkImageView& GetSecondaryImageView()
    {
      return m_SecondaryImageView;
    }

    VkImageLayout& GetSecondaryLayout()
    {
      return m_SecondaryImageLayout;
    }

    VkSampler& GetSecondarySampler()
    {
      return m_SecondarySampler;
    }

    VkImage& GetSecondaryImage()
    {
      return m_SecondaryImage;
    }

  private:

    bool b_SecondaryBuffer = false;

    VkFramebuffer m_Framebuffer {};

    VkImage m_Image {};
    VkImageView m_ImageView {};
    VkImageLayout m_ImageLayout {};
    VkSampler m_Sampler {};
    VmaAllocation m_ImageAllocation {};

    VkImageView m_DepthImageView {};
    VkImage m_DepthImage {};
    VkImageLayout m_DepthImageLayout {};
    VkSampler m_DepthSampler {};
    VmaAllocation m_DepthImageAllocation {};

    VkImageView m_SecondaryImageView {};
    VkImage m_SecondaryImage {};
    VkImageLayout m_SecondaryImageLayout {};
    VkSampler m_SecondarySampler {};
    VmaAllocation m_SecondaryImageAllocation {};

    VkRenderPass m_RenderPass {};

    VkFormat m_Format {};
    VmaAllocator m_Allocator {};
    VkDevice m_Device {};
  };
}