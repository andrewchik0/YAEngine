#include "VulkanDevice.h"

#include "VulkanPhysicalDevice.h"
#include "VulkanInstance.h"
#include "Utils/Log.h"

namespace YAEngine
{
  namespace
  {
    uint32_t PickComputeFamily(const std::vector<VkQueueFamilyProperties>& families, uint32_t graphicsFamily)
    {
      for (uint32_t i = 0; i < static_cast<uint32_t>(families.size()); i++)
      {
        bool compute = (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
        bool graphics = (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        if (compute && !graphics && families[i].queueCount > 0)
          return i;
      }

      return graphicsFamily;
    }
  }

  void VulkanDevice::Init(VulkanInstance& instance, VulkanPhysicalDevice physicalDevice, VkSurfaceKHR& surface,
                          VulkanRequirements& requirements)
  {
    QueueFamilyIndices indices = VulkanPhysicalDevice::FindQueueFamilies(physicalDevice.Get(), surface);

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice.Get(), &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> familyProps(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice.Get(), &familyCount, familyProps.data());

    const uint32_t graphicsFamily = indices.graphicsFamily.value();

    std::map<uint32_t, uint32_t> queuesPerFamily;
    queuesPerFamily[graphicsFamily] = 1;
    queuesPerFamily[indices.presentFamily.value()] = 1;

    m_GraphicsQueue = { .family = graphicsFamily, .index = 0 };

    auto reserveQueues = [&](uint32_t family, uint32_t count, std::vector<VulkanQueueLocation>& out)
    {
      if (count == 0)
        return;

      const uint32_t capacity = family < familyCount ? familyProps[family].queueCount : 0;
      uint32_t& used = queuesPerFamily[family];
      const uint32_t granted = std::min(count, capacity > used ? capacity - used : 0);

      for (uint32_t i = 0; i < granted; i++)
        out.push_back({ .family = family, .index = used + i });

      used += granted;

      if (granted < count)
        YA_LOG_WARN("Vulkan", "Queue family %u can only provide %u of the %u extra queues requested", family, granted, count);
    };

    reserveQueues(graphicsFamily, requirements.GetExtraGraphicsQueues(), m_ExtraGraphicsQueues);
    reserveQueues(PickComputeFamily(familyProps, graphicsFamily), requirements.GetExtraComputeQueues(), m_ExtraComputeQueues);

    std::vector<std::vector<float>> queuePriorities;
    queuePriorities.reserve(queuesPerFamily.size());
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    queueCreateInfos.reserve(queuesPerFamily.size());

    for (const auto& [family, count] : queuesPerFamily)
    {
      queuePriorities.emplace_back(count, 1.0f);

      VkDeviceQueueCreateInfo queueCreateInfo{};
      queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      queueCreateInfo.queueFamilyIndex = family;
      queueCreateInfo.queueCount = count;
      queueCreateInfo.pQueuePriorities = queuePriorities.back().data();
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

    VkPhysicalDeviceVulkan13Features supported13 { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceVulkan12Features supported12 { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &supported13 };
    VkPhysicalDeviceVulkan11Features supported11 { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, .pNext = &supported12 };
    VkPhysicalDeviceFeatures2 supportedFeatures2 { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &supported11 };
    vkGetPhysicalDeviceFeatures2(physicalDevice.Get(), &supportedFeatures2);

    const VkPhysicalDeviceFeatures& supported = supportedFeatures2.features;
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
    // The quantized shadow position stream is fed as a vertex attribute, and only
    // the buffer features of the format say whether that is allowed at all.
    VkFormatProperties unorm16Props {};
    vkGetPhysicalDeviceFormatProperties(physicalDevice.Get(), VK_FORMAT_R16G16B16A16_UNORM, &unorm16Props);
    b_Unorm16VertexSupported = (unorm16Props.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) != 0;

    b_DepthClampSupported = deviceFeatures.depthClamp == VK_TRUE;
    b_MultiDrawIndirectSupported = deviceFeatures.multiDrawIndirect == VK_TRUE;
    b_DrawIndirectFirstInstanceSupported = deviceFeatures.drawIndirectFirstInstance == VK_TRUE;

    uint32_t droppedFeatures = requirements.DropUnsupportedFeatures(supported11, supported12, supported13);
    if (droppedFeatures > 0)
      YA_LOG_WARN("Vulkan", "%u requested optional Vulkan feature(s) are unsupported and stay disabled", droppedFeatures);

    VkPhysicalDeviceVulkan11Features features11 = requirements.GetVulkan11Features();
    VkPhysicalDeviceVulkan12Features features12 = requirements.GetVulkan12Features();
    VkPhysicalDeviceVulkan13Features features13 = requirements.GetVulkan13Features();

    features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features11.pNext = &features12;
    features12.pNext = &features13;
    features13.pNext = nullptr;

    VkPhysicalDeviceFeatures2 features2 { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &features11 };
    features2.features = deviceFeatures;

    std::vector<const char*> extensionNames;
    extensionNames.reserve(physicalDevice.GetEnabledExtensions().size());
    for (const auto& name : physicalDevice.GetEnabledExtensions())
      extensionNames.push_back(name.c_str());

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features2;

    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());

    createInfo.pEnabledFeatures = nullptr;

    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensionNames.size());
    createInfo.ppEnabledExtensionNames = extensionNames.data();

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
