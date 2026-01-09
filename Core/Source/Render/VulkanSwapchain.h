#pragma once

namespace YAEngine
{
  class VulkanSwapChain
  {
  public:
    void Init(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, GLFWwindow* window, VmaAllocator allocator);
    void Destroy();
    void Recreate(VkRenderPass renderPass, uint32_t width, uint32_t height);

    void CreateFrameBuffers(VkRenderPass renderPass, uint32_t width, uint32_t height);

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

    VkImageView GetDepthView()
    {
      return m_DepthImageView;
    }

    VkImageView GetMultisampleView()
    {
      return m_MultisampleImageView;
    }


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

    VkImage m_MultisampleImage {};
    VmaAllocation m_MultisampleImageAllocation {};
    VkImageView m_MultisampleImageView {};

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
  };
}