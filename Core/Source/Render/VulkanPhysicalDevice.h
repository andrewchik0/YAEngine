#pragma once

#include "Pch.h"
#include "VulkanRequirements.h"

namespace YAEngine
{
  struct QueueFamilyIndices
  {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    [[nodiscard]] bool IsComplete() const
    {
      return graphicsFamily.has_value() && presentFamily.has_value();
    }
  };

  struct SwapChainSupportDetails
  {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
  };

  class VulkanPhysicalDevice
  {
  public:

    const std::vector<const char*> m_DeviceExtensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
      VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
      VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME
    };

    void Init(VkInstance instance, VkSurfaceKHR surface, VulkanRequirements& requirements);

    VkPhysicalDevice& Get()
    {
      return m_PhysicalDevice;
    }

    // Base list plus the optional extensions the selected device turned out to support.
    const std::vector<std::string>& GetEnabledExtensions() const
    {
      return m_EnabledExtensions;
    }

    static QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);
    static SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);
  private:

    bool IsDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface);
    bool CheckDeviceExtensionSupport(VkPhysicalDevice device);
    void ResolveExtensions(VulkanRequirements& requirements);

    VkPhysicalDevice m_PhysicalDevice {};
    std::vector<std::string> m_EnabledExtensions;

    VkInstance m_Instance {};
    VkSurfaceKHR m_Surface {};
  };
}
