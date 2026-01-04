#pragma once
#include <optional>

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

    void Init(VkInstance instance, VkSurfaceKHR surface);

    VkPhysicalDevice& Get()
    {
      return m_PhysicalDevice;
    }

    static QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);
    static SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);
  private:

    bool IsDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface);
    bool CheckDeviceExtensionSupport(VkPhysicalDevice device);

    VkPhysicalDevice m_PhysicalDevice {};

    VkInstance m_Instance {};
    VkSurfaceKHR m_Surface {};
  };
}
