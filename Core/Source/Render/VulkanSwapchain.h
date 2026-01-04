#pragma once

namespace YAEngine
{
  class VulkanSwapChain
  {
  public:
    void Init(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, GLFWwindow* window);
    void CreateFrameBuffers(VkRenderPass renderPass, VkImageView depthView);
    void Destroy();
    void Recreate(VkRenderPass renderPass, VkImageView depthView);

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

  private:
    VkSwapchainKHR m_SwapChain {};
    std::vector<VkImage> m_SwapChainImages;
    std::vector<VkImageView> m_SwapChainImageViews;
    VkFormat m_SwapChainImageFormat {};
    std::vector<VkFramebuffer> m_SwapChainFrameBuffers;
    VkExtent2D m_SwapChainExtent {};

    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window);

    void CreateImageViews(VkDevice device);

    VkDevice m_Device {};
    VkSurfaceKHR m_Surface {};
    VkPhysicalDevice m_PhysicalDevice {};
    VkRenderPass m_RenderPass {};
    GLFWwindow* m_Window {};
  };
}