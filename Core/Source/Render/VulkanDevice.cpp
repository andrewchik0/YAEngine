#include "VulkanDevice.h"

#include "VulkanPhysicalDevice.h"
#include "VulkanInstance.h"

namespace YAEngine
{
  void VulkanDevice::Init(VulkanInstance& instance, VulkanPhysicalDevice physicalDevice, VkSurfaceKHR& surface)
  {
    QueueFamilyIndices indices = VulkanPhysicalDevice::FindQueueFamilies(physicalDevice.Get(), surface);

    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = indices.graphicsFamily.value();
    queueCreateInfo.queueCount = 1;

    float queuePriority = 1.0f;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;

    createInfo.pEnabledFeatures = &deviceFeatures;

    createInfo.enabledExtensionCount = static_cast<uint32_t>(physicalDevice.m_DeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = physicalDevice.m_DeviceExtensions.data();

    if (instance.m_DebugExtension.b_Enabled)
    {
      createInfo.enabledLayerCount = 1;
      createInfo.ppEnabledLayerNames = &instance.m_DebugExtension.m_LayerName;
    } else
    {
      createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(physicalDevice.Get(), &createInfo, nullptr, &m_Device) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create logical device!");
    }
  }

  void VulkanDevice::Destroy()
  {
    vkDestroyDevice(m_Device, nullptr);
  }
}
