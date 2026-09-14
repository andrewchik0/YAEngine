#pragma once

namespace YAEngine
{
  class VulkanSwapChain
  {
  public:
    void Init(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, GLFWwindow* window, VmaAllocator allocator);
    void Destroy();
    // False, with the old swapchain left as it was, while the surface has a zero extent (a
    // minimized window).
    bool Recreate(VkRenderPass renderPass);

    void CreateFrameBuffers(VkRenderPass renderPass);

    VkSwapchainKHR& Get()
    {
      return m_SwapChain;
    }

    VkFormat& GetFormat()
    {
      return m_SwapChainImageFormat;
    }

    VkExtent2D& GetExt()
    {
      return m_SwapChainExtent;
    }

    VkFramebuffer& GetFramebuffer(size_t index)
    {
      return m_SwapChainFrameBuffers[index];
    }

    uint32_t GetImageCount()
    {
      return (uint32_t)m_SwapChainImages.size();
    }

#ifdef YA_EDITOR
    VkImage GetImage(size_t index) const
    {
      return m_SwapChainImages[index];
    }

    // The agent bridge reads presented frames back; not every surface allows copying from them.
    bool SupportsTransferSource() const
    {
      return b_TransferSource;
    }
#endif

  private:
    VkSwapchainKHR m_SwapChain {};
    std::vector<VkImage> m_SwapChainImages;
    std::vector<VkImageView> m_SwapChainImageViews;
    VkFormat m_SwapChainImageFormat {};
    std::vector<VkFramebuffer> m_SwapChainFrameBuffers;
    VkExtent2D m_SwapChainExtent {};

    VkImage m_DepthImage {};
    VmaAllocation m_DepthImageAllocation {};
    VkImageView m_DepthImageView {};

    VkImageUsageFlags ChooseImageUsage(const VkSurfaceCapabilitiesKHR& capabilities) const;
    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window);

    void CreateImageViews(VkDevice device);

    VkDevice m_Device {};
    VkSurfaceKHR m_Surface {};
    VmaAllocator m_Allocator {};
    VkPhysicalDevice m_PhysicalDevice {};
    VkRenderPass m_RenderPass {};
    GLFWwindow* m_Window {};
#ifdef YA_EDITOR
    bool b_TransferSource = false;
#endif
  };
}