#include "VulkanDevice.h"

#include "VulkanPhysicalDevice.h"
#include "VulkanInstance.h"
#include "Utils/Log.h"

namespace YAEngine
{
  void VulkanDevice::Init(VulkanInstance& instance, VulkanPhysicalDevice physicalDevice, VkSurfaceKHR& surface)
  {
    QueueFamilyIndices indices = VulkanPhysicalDevice::FindQueueFamilies(physicalDevice.Get(), surface);

    std::set<uint32_t> uniqueFamilies = {
      indices.graphicsFamily.value(),
      indices.presentFamily.value()
    };

    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    for (uint32_t family : uniqueFamilies)
    {
      VkDeviceQueueCreateInfo queueCreateInfo{};
      queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      queueCreateInfo.queueFamilyIndex = family;
      queueCreateInfo.queueCount = 1;
      queueCreateInfo.pQueuePriorities = &queuePriority;
      queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.wideLines = VK_TRUE;
    deviceFeatures.fillModeNonSolid = VK_TRUE;
    deviceFeatures.imageCubeArray = VK_TRUE;
    deviceFeatures.textureCompressionBC = VK_TRUE;
    // Shadow pipelines clamp depth instead of clipping it: a caster sitting in front of
    // a cascade near plane still has to occlude, not disappear from the shadow map.
    deviceFeatures.depthClamp = VK_TRUE;
    // Batched shadow casters issue one vkCmdDrawIndexedIndirect per atlas tile with many
    // commands in it, and each command picks its model matrix through firstInstance.
    deviceFeatures.multiDrawIndirect = VK_TRUE;
    deviceFeatures.drawIndirectFirstInstance = VK_TRUE;

    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(physicalDevice.Get(), &supported);
    if (supported.textureCompressionBC == VK_FALSE)
    {
      YA_LOG_ERROR("Vulkan", "Device reports no BC texture compression support, DDS textures will fail to load");
      deviceFeatures.textureCompressionBC = VK_FALSE;
    }
    if (supported.depthClamp == VK_FALSE)
    {
      YA_LOG_WARN("Vulkan", "Device reports no depth clamp support, shadow casters in front of a cascade near plane will be clipped away");
      deviceFeatures.depthClamp = VK_FALSE;
    }
    if (supported.multiDrawIndirect == VK_FALSE)
    {
      YA_LOG_WARN("Vulkan", "Device reports no multiDrawIndirect support, indirect shadow batching stays disabled");
      deviceFeatures.multiDrawIndirect = VK_FALSE;
    }
    if (supported.drawIndirectFirstInstance == VK_FALSE)
    {
      YA_LOG_WARN("Vulkan", "Device reports no drawIndirectFirstInstance support, indirect shadow batching stays disabled");
      deviceFeatures.drawIndirectFirstInstance = VK_FALSE;
    }
    b_DepthClampSupported = deviceFeatures.depthClamp == VK_TRUE;
    b_MultiDrawIndirectSupported = deviceFeatures.multiDrawIndirect == VK_TRUE;
    b_DrawIndirectFirstInstanceSupported = deviceFeatures.drawIndirectFirstInstance == VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());

    createInfo.pEnabledFeatures = &deviceFeatures;

    createInfo.enabledExtensionCount = static_cast<uint32_t>(physicalDevice.m_DeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = physicalDevice.m_DeviceExtensions.data();

    if (instance.m_DebugExtension.b_ValidationLayers)
    {
      createInfo.enabledLayerCount = 1;
      createInfo.ppEnabledLayerNames = &instance.m_DebugExtension.m_LayerName;
    } else
    {
      createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(physicalDevice.Get(), &createInfo, nullptr, &m_Device) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create logical device");
      throw std::runtime_error("failed to create logical device!");
    }
  }

  void VulkanDevice::Destroy()
  {
    vkDestroyDevice(m_Device, nullptr);
  }
}
