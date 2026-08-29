#include "VulkanPhysicalDevice.h"

#include "Utils/Log.h"

namespace YAEngine
{
  void VulkanPhysicalDevice::Init(VkInstance instance, VkSurfaceKHR surface, VulkanRequirements& requirements)
  {
    m_Instance = instance;
    m_Surface = surface;

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
      YA_LOG_ERROR("Render", "No GPUs with Vulkan support found");
      throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

    for (const auto& device : devices)
    {
      if (IsDeviceSuitable(device, m_Surface))
      {
        m_PhysicalDevice = device;
        break;
      }
    }

    if (m_PhysicalDevice == VK_NULL_HANDLE)
    {
      YA_LOG_ERROR("Render", "No suitable GPU found");
      throw std::runtime_error("failed to find a suitable GPU!");
    }

    ResolveExtensions(requirements);
  }

  void VulkanPhysicalDevice::ResolveExtensions(VulkanRequirements& requirements)
  {
    m_EnabledExtensions.assign(m_DeviceExtensions.begin(), m_DeviceExtensions.end());

    if (requirements.GetDeviceExtensions().empty())
      return;

    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> supported;
    for (const auto& extension : availableExtensions)
      supported.insert(extension.extensionName);

    for (auto& request : requirements.GetDeviceExtensions())
    {
      // VUID-VkDeviceCreateInfo-pNext-04748 forbids listing the pre-promotion extension
      // next to a VkPhysicalDeviceVulkan12Features with bufferDeviceAddress enabled, and
      // the device always carries that struct. The core feature covers the request.
      if (request.name == VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME
        && requirements.GetVulkan12Features().bufferDeviceAddress == VK_TRUE)
      {
        request.enabled = true;
        YA_LOG_INFO("Vulkan", "Skipping %s, the core Vulkan 1.2 bufferDeviceAddress feature replaces it",
          request.name.c_str());
        continue;
      }

      if (std::find(m_EnabledExtensions.begin(), m_EnabledExtensions.end(), request.name) != m_EnabledExtensions.end())
      {
        request.enabled = true;
        continue;
      }

      if (!supported.contains(request.name))
      {
        YA_LOG_WARN("Vulkan", "Optional device extension %s is not supported, skipping it", request.name.c_str());
        continue;
      }

      request.enabled = true;
      m_EnabledExtensions.push_back(request.name);
      YA_LOG_INFO("Vulkan", "Enabling optional device extension %s", request.name.c_str());
    }
  }

  bool VulkanPhysicalDevice::IsDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices = FindQueueFamilies(device, surface);

    bool extensionsSupported = CheckDeviceExtensionSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported)
    {
      SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(device, surface);
      swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    return indices.IsComplete() && extensionsSupported && swapChainAdequate;
  }

  QueueFamilyIndices VulkanPhysicalDevice::FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies)
    {
      if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
      {
        indices.graphicsFamily = i;
      }

      VkBool32 presentSupport = false;
      vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

      if (presentSupport)
      {
        indices.presentFamily = i;
      }

      if (indices.IsComplete())
      {
        break;
      }

      i++;
    }

    return indices;
  }

  bool VulkanPhysicalDevice::CheckDeviceExtensionSupport(VkPhysicalDevice device)
  {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(m_DeviceExtensions.begin(), m_DeviceExtensions.end());

    for (const auto& extension : availableExtensions)
    {
      requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
  }

  SwapChainSupportDetails VulkanPhysicalDevice::QuerySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

    if (formatCount != 0)
    {
      details.formats.resize(formatCount);
      vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

    if (presentModeCount != 0)
    {
      details.presentModes.resize(presentModeCount);
      vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
  }
}
