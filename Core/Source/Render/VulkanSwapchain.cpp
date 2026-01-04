#include "VulkanSwapchain.h"

#include <algorithm>
#include <limits>

#include "VulkanPhysicalDevice.h"

namespace YAEngine
{
  void VulkanSwapChain::Init(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, GLFWwindow* window)
  {
    m_Device = device;
    m_Surface = surface;
    m_PhysicalDevice = physicalDevice;
    m_Window = window;

    SwapChainSupportDetails swapChainSupport = VulkanPhysicalDevice::QuerySwapChainSupport(m_PhysicalDevice, m_Surface);

    VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = ChooseSwapExtent(swapChainSupport.capabilities, window);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount)
    {
      imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_Surface;

    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices indices = VulkanPhysicalDevice::FindQueueFamilies(m_PhysicalDevice, m_Surface);
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    if (indices.graphicsFamily != indices.presentFamily)
    {
      createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
      createInfo.queueFamilyIndexCount = 2;
      createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
      createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &m_SwapChain) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create swap chain!");
    }

    vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &imageCount, nullptr);
    m_SwapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &imageCount, m_SwapChainImages.data());

    m_SwapChainImageFormat = surfaceFormat.format;
    m_SwapChainExtent = extent;

    CreateImageViews(m_Device);
  }

  VkSurfaceFormatKHR VulkanSwapChain::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
  {
    for (const auto& availableFormat : availableFormats)
    {
      if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
      {
        return availableFormat;
      }
    }

    return availableFormats[0];
  }

  VkPresentModeKHR VulkanSwapChain::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
  {
    for (const auto& availablePresentMode : availablePresentModes)
    {
      if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
      {
        return availablePresentMode;
      }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
  }

  VkExtent2D VulkanSwapChain::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window)
  {
    if (capabilities.currentExtent.width != (std::numeric_limits<uint32_t>::max)())
    {
      return capabilities.currentExtent;
    }
    else
    {
      int width, height;
      glfwGetFramebufferSize(window, &width, &height);

      VkExtent2D actualExtent = {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
      };

      actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
      actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

      return actualExtent;
    }
  }

  void VulkanSwapChain::CreateImageViews(VkDevice device)
  {
    m_SwapChainImageViews.resize(m_SwapChainImages.size());

    for (size_t i = 0; i < m_SwapChainImages.size(); i++)
    {
      VkImageViewCreateInfo createInfo{};
      createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      createInfo.image = m_SwapChainImages[i];
      createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      createInfo.format = m_SwapChainImageFormat;
      createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
      createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
      createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
      createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
      createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      createInfo.subresourceRange.baseMipLevel = 0;
      createInfo.subresourceRange.levelCount = 1;
      createInfo.subresourceRange.baseArrayLayer = 0;
      createInfo.subresourceRange.layerCount = 1;

      if (vkCreateImageView(device, &createInfo, nullptr, &m_SwapChainImageViews[i]) != VK_SUCCESS)
      {
        throw std::runtime_error("failed to create image views!");
      }
    }
  }

  void VulkanSwapChain::CreateFrameBuffers(VkRenderPass renderPass, VkImageView depthView)
  {
    m_RenderPass = renderPass;
    m_SwapChainFrameBuffers.resize(m_SwapChainImageViews.size());

    for (size_t i = 0; i < m_SwapChainImageViews.size(); i++)
    {
      VkImageView attachments[] = {
        m_SwapChainImageViews[i],
        depthView
    };

      VkFramebufferCreateInfo framebufferInfo{};
      framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      framebufferInfo.renderPass = m_RenderPass;
      framebufferInfo.attachmentCount = 2;
      framebufferInfo.pAttachments = attachments;
      framebufferInfo.width = m_SwapChainExtent.width;
      framebufferInfo.height = m_SwapChainExtent.height;
      framebufferInfo.layers = 1;

      if (vkCreateFramebuffer(m_Device, &framebufferInfo, nullptr, &m_SwapChainFrameBuffers[i]) != VK_SUCCESS)
      {
        throw std::runtime_error("failed to create framebuffer!");
      }
    }
  }

  void VulkanSwapChain::Destroy()
  {
    for (auto framebuffer : m_SwapChainFrameBuffers)
    {
      vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
    }

    for (auto imageView : m_SwapChainImageViews)
    {
      vkDestroyImageView(m_Device, imageView, nullptr);
    }
    vkDestroySwapchainKHR(m_Device, m_SwapChain, nullptr);
  }

  void VulkanSwapChain::Recreate(VkRenderPass renderPass, VkImageView depthView)
  {
    m_RenderPass = renderPass;
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_Window, &width, &height);
    while (width == 0 || height == 0) {
      glfwGetFramebufferSize(m_Window, &width, &height);
      glfwWaitEvents();
    }

    vkDeviceWaitIdle(m_Device);

    Destroy();
    Init(m_Device, m_PhysicalDevice, m_Surface, m_Window);
    CreateFrameBuffers(m_RenderPass, depthView);
  }
}
