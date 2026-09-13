#include "VulkanRequirements.h"

#include "Utils/Log.h"

namespace YAEngine
{
  namespace
  {
    VulkanRequirements::ExtensionRequest* FindOrAdd(std::vector<VulkanRequirements::ExtensionRequest>& requests,
                                                    const char* name)
    {
      if (name == nullptr || name[0] == '\0')
        return nullptr;

      for (auto& request : requests)
      {
        if (request.name == name)
          return &request;
      }

      requests.push_back({ .name = name });
      return &requests.back();
    }

    void AddUniqueName(std::vector<std::string>& names, const char* name)
    {
      if (name == nullptr || name[0] == '\0')
        return;

      if (std::find(names.begin(), names.end(), name) == names.end())
        names.emplace_back(name);
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
    FindOrAdd(m_InstanceExtensions, name);
  }

  void VulkanRequirements::AddDeviceExtension(const char* name, std::initializer_list<const char*> dependencies)
  {
    ExtensionRequest* request = FindOrAdd(m_DeviceExtensions, name);
    if (request == nullptr)
      return;

    for (const char* dependency : dependencies)
      AddUniqueName(request->dependencies, dependency);
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

  void VulkanRequirements::AddDeviceFeatureStruct(const void* features, size_t size,
                                                  std::initializer_list<const char*> extensions)
  {
    if (features == nullptr || size <= sizeof(FeatureStructHeader))
      return;

    const VkStructureType type = *static_cast<const VkStructureType*>(features);

    FeatureStructRequest* request = nullptr;
    for (auto& candidate : m_DeviceFeatureStructs)
    {
      if (candidate.type == type)
      {
        request = &candidate;
        break;
      }
    }

    if (request == nullptr)
    {
      m_DeviceFeatureStructs.push_back({ .type = type });
      request = &m_DeviceFeatureStructs.back();
    }

    if (size > request->size)
    {
      request->size = size;
      request->storage.resize((size + sizeof(uint64_t) - 1) / sizeof(uint64_t), 0);
      *static_cast<VkStructureType*>(request->Data()) = type;
    }

    MergeFeatures(request->Data(), features, size);

    for (const char* extension : extensions)
    {
      AddUniqueName(request->extensions, extension);
      AddDeviceExtension(extension);
    }
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

  uint32_t VulkanRequirements::ResolveDeviceFeatureStructs(VkPhysicalDevice physicalDevice)
  {
    for (auto& request : m_DeviceFeatureStructs)
    {
      request.enabled = true;

      for (const auto& extension : request.extensions)
      {
        if (IsDeviceExtensionEnabled(extension.c_str()))
          continue;

        // Only masked bits count as unsatisfied features, the same way a dropped extension
        // does not: the struct is going away because its extension already did.
        request.enabled = false;
        YA_LOG_WARN("Vulkan", "Optional feature struct of %s is dropped, the extension is not enabled",
          extension.c_str());
        break;
      }
    }

    // Mirroring the chain with zero-initialized copies is the only way to learn which bits
    // the driver grants: vkGetPhysicalDeviceFeatures2 leaves a struct it does not know
    // untouched, so a zero start reads as unsupported either way.
    std::vector<std::vector<uint64_t>> supportedStorage;
    supportedStorage.reserve(m_DeviceFeatureStructs.size());

    void* head = nullptr;
    for (auto& request : m_DeviceFeatureStructs)
    {
      if (!request.enabled)
        continue;

      supportedStorage.emplace_back(request.storage.size(), 0ULL);
      void* copy = supportedStorage.back().data();

      auto* header = static_cast<FeatureStructHeader*>(copy);
      header->sType = request.type;
      header->pNext = head;
      head = copy;
    }

    if (head == nullptr)
      return 0;

    VkPhysicalDeviceFeatures2 supportedFeatures2 { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = head };
    vkGetPhysicalDeviceFeatures2(physicalDevice, &supportedFeatures2);

    uint32_t dropped = 0;
    size_t index = 0;
    for (auto& request : m_DeviceFeatureStructs)
    {
      if (!request.enabled)
        continue;

      dropped += DropUnsupported(request.Data(), supportedStorage[index].data(), request.size);
      index++;
    }

    if (dropped > 0)
      b_FeaturesSatisfied = false;

    return dropped;
  }

  void* VulkanRequirements::BuildDeviceFeatureStructChain()
  {
    void* head = nullptr;
    for (auto& request : m_DeviceFeatureStructs)
    {
      if (!request.enabled)
        continue;

      auto* header = static_cast<FeatureStructHeader*>(request.Data());
      header->pNext = head;
      head = request.Data();
    }

    return head;
  }

  const void* VulkanRequirements::FindDeviceFeatureStruct(VkStructureType type) const
  {
    for (const auto& request : m_DeviceFeatureStructs)
    {
      if (request.type == type)
        return request.enabled ? request.Data() : nullptr;
    }

    return nullptr;
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
