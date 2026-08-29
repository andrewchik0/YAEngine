#include "VulkanRequirements.h"

namespace YAEngine
{
  namespace
  {
    void AddUnique(std::vector<VulkanRequirements::ExtensionRequest>& requests, const char* name)
    {
      if (name == nullptr || name[0] == '\0')
        return;

      for (const auto& request : requests)
      {
        if (request.name == name)
          return;
      }

      requests.push_back({ .name = name });
    }

    struct FeatureStructHeader
    {
      VkStructureType sType;
      void* pNext;
    };

    // Vulkan version feature structs are a flat run of VkBool32 flags behind the common
    // sType/pNext header, so requested sets can be merged and masked without spelling out
    // every field of every version.
    VkBool32* FeatureFlags(void* featureStruct)
    {
      return reinterpret_cast<VkBool32*>(static_cast<uint8_t*>(featureStruct) + sizeof(FeatureStructHeader));
    }

    const VkBool32* FeatureFlags(const void* featureStruct)
    {
      return reinterpret_cast<const VkBool32*>(static_cast<const uint8_t*>(featureStruct) + sizeof(FeatureStructHeader));
    }

    size_t FeatureFlagCount(size_t structSize)
    {
      return (structSize - sizeof(FeatureStructHeader)) / sizeof(VkBool32);
    }

    void MergeFeatures(void* destination, const void* source, size_t structSize)
    {
      VkBool32* destinationFlags = FeatureFlags(destination);
      const VkBool32* sourceFlags = FeatureFlags(source);

      for (size_t i = 0; i < FeatureFlagCount(structSize); i++)
      {
        if (sourceFlags[i] == VK_TRUE)
          destinationFlags[i] = VK_TRUE;
      }
    }

    uint32_t DropUnsupported(void* requested, const void* supported, size_t structSize)
    {
      VkBool32* requestedFlags = FeatureFlags(requested);
      const VkBool32* supportedFlags = FeatureFlags(supported);

      uint32_t dropped = 0;
      for (size_t i = 0; i < FeatureFlagCount(structSize); i++)
      {
        if (requestedFlags[i] == VK_TRUE && supportedFlags[i] == VK_FALSE)
        {
          requestedFlags[i] = VK_FALSE;
          dropped++;
        }
      }

      return dropped;
    }
  }

  void VulkanRequirements::AddInstanceExtension(const char* name)
  {
    AddUnique(m_InstanceExtensions, name);
  }

  void VulkanRequirements::AddDeviceExtension(const char* name)
  {
    AddUnique(m_DeviceExtensions, name);
  }

  void VulkanRequirements::MergeVulkan11Features(const VkPhysicalDeviceVulkan11Features& features)
  {
    MergeFeatures(&m_Vulkan11Features, &features, sizeof(features));
  }

  void VulkanRequirements::MergeVulkan12Features(const VkPhysicalDeviceVulkan12Features& features)
  {
    MergeFeatures(&m_Vulkan12Features, &features, sizeof(features));
  }

  void VulkanRequirements::MergeVulkan13Features(const VkPhysicalDeviceVulkan13Features& features)
  {
    MergeFeatures(&m_Vulkan13Features, &features, sizeof(features));
  }

  uint32_t VulkanRequirements::DropUnsupportedFeatures(const VkPhysicalDeviceVulkan11Features& supported11,
                                                       const VkPhysicalDeviceVulkan12Features& supported12,
                                                       const VkPhysicalDeviceVulkan13Features& supported13)
  {
    uint32_t dropped = 0;
    dropped += DropUnsupported(&m_Vulkan11Features, &supported11, sizeof(m_Vulkan11Features));
    dropped += DropUnsupported(&m_Vulkan12Features, &supported12, sizeof(m_Vulkan12Features));
    dropped += DropUnsupported(&m_Vulkan13Features, &supported13, sizeof(m_Vulkan13Features));

    if (dropped > 0)
      b_FeaturesSatisfied = false;

    return dropped;
  }

  void VulkanRequirements::RequestExtraGraphicsQueues(uint32_t count)
  {
    m_ExtraGraphicsQueues += count;
  }

  void VulkanRequirements::RequestExtraComputeQueues(uint32_t count)
  {
    m_ExtraComputeQueues += count;
  }

  bool VulkanRequirements::IsInstanceExtensionEnabled(const char* name) const
  {
    return IsEnabled(m_InstanceExtensions, name);
  }

  bool VulkanRequirements::IsDeviceExtensionEnabled(const char* name) const
  {
    return IsEnabled(m_DeviceExtensions, name);
  }

  bool VulkanRequirements::IsEnabled(const std::vector<ExtensionRequest>& requests, const char* name)
  {
    if (name == nullptr)
      return false;

    for (const auto& request : requests)
    {
      if (request.name == name)
        return request.enabled;
    }

    return false;
  }
}
