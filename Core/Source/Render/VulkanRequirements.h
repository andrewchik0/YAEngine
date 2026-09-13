#pragma once

#include "Pch.h"

namespace YAEngine
{
  // Instance/device extensions, Vulkan feature bits and extra queues that optional
  // subsystems register before the instance and the device are created. Everything
  // registered here is optional: an entry the driver does not expose is dropped with a
  // warning and marked unavailable, it never makes a physical device unsuitable.
  class VulkanRequirements
  {
  public:

    struct ExtensionRequest
    {
      std::string name;
      // Device extensions this one may not be enabled without.
      std::vector<std::string> dependencies;
      bool enabled = false;
    };

    // A feature struct belonging to a device extension, kept as a sized blob so any
    // VkPhysicalDevice*FeaturesKHR type can be requested without naming it here.
    struct FeatureStructRequest
    {
      VkStructureType type {};
      size_t size = 0;
      // Extensions that must all be enabled for the struct to be chained at all.
      std::vector<std::string> extensions;
      // uint64_t elements so the blob is aligned for the pointer in the struct header.
      // The buffer address survives the outer vector growing, which is what keeps the
      // chain built at device creation valid.
      std::vector<uint64_t> storage;
      bool enabled = false;

      void* Data() { return storage.data(); }
      const void* Data() const { return storage.data(); }
    };

    void AddInstanceExtension(const char* name);
    void AddDeviceExtension(const char* name, std::initializer_list<const char*> dependencies = {});

    // Callers set the individual VkBool32 fields they need to VK_TRUE. Bits the device
    // does not support are cleared at device creation time.
    VkPhysicalDeviceVulkan11Features& GetVulkan11Features() { return m_Vulkan11Features; }
    VkPhysicalDeviceVulkan12Features& GetVulkan12Features() { return m_Vulkan12Features; }
    VkPhysicalDeviceVulkan13Features& GetVulkan13Features() { return m_Vulkan13Features; }

    const VkPhysicalDeviceVulkan11Features& GetVulkan11Features() const { return m_Vulkan11Features; }
    const VkPhysicalDeviceVulkan12Features& GetVulkan12Features() const { return m_Vulkan12Features; }
    const VkPhysicalDeviceVulkan13Features& GetVulkan13Features() const { return m_Vulkan13Features; }

    void MergeVulkan11Features(const VkPhysicalDeviceVulkan11Features& features);
    void MergeVulkan12Features(const VkPhysicalDeviceVulkan12Features& features);
    void MergeVulkan13Features(const VkPhysicalDeviceVulkan13Features& features);

    // Registers an extension feature struct, identified by the sType the caller filled in.
    // Repeated registrations of the same sType OR their VkBool32 fields together, exactly
    // like the Vulkan 1.x merges above, and the owning extensions are requested along with
    // it so a struct can never be chained without them.
    void AddDeviceFeatureStruct(const void* features, size_t size, std::initializer_list<const char*> extensions);

    template<typename T>
    void AddDeviceFeatureStruct(const T& features, std::initializer_list<const char*> extensions)
    {
      AddDeviceFeatureStruct(&features, sizeof(T), extensions);
    }

    // Clears every requested bit the device does not expose and returns how many were
    // dropped, so an unsupported optional feature costs a warning instead of a failure.
    uint32_t DropUnsupportedFeatures(const VkPhysicalDeviceVulkan11Features& supported11,
                                     const VkPhysicalDeviceVulkan12Features& supported12,
                                     const VkPhysicalDeviceVulkan13Features& supported13);

    // Per-struct analog of the above: drops whole structs whose extensions the device did
    // not grant, then masks the surviving ones against a zero-initialized mirror of the
    // chain read back through vkGetPhysicalDeviceFeatures2. Returns the bits dropped.
    // Must run after the physical device resolved the extension requests.
    uint32_t ResolveDeviceFeatureStructs(VkPhysicalDevice physicalDevice);

    // Links the surviving structs and returns the head of the chain, or nullptr. Valid
    // until another struct is registered.
    void* BuildDeviceFeatureStructChain();

    // The resolved struct, or nullptr when it was dropped. The bits inside are what the
    // device actually granted, so a caller only has to test the field it cares about.
    const void* FindDeviceFeatureStruct(VkStructureType type) const;

    // Queues on top of the single graphics queue the engine always creates, requests accumulate.
    void RequestExtraGraphicsQueues(uint32_t count);
    void RequestExtraComputeQueues(uint32_t count);

    uint32_t GetExtraGraphicsQueues() const { return m_ExtraGraphicsQueues; }
    uint32_t GetExtraComputeQueues() const { return m_ExtraComputeQueues; }

    std::vector<ExtensionRequest>& GetInstanceExtensions() { return m_InstanceExtensions; }
    std::vector<ExtensionRequest>& GetDeviceExtensions() { return m_DeviceExtensions; }

    bool IsInstanceExtensionEnabled(const char* name) const;
    bool IsDeviceExtensionEnabled(const char* name) const;

    bool AreFeaturesSatisfied() const { return b_FeaturesSatisfied; }

  private:

    static bool IsEnabled(const std::vector<ExtensionRequest>& requests, const char* name);

    std::vector<ExtensionRequest> m_InstanceExtensions;
    std::vector<ExtensionRequest> m_DeviceExtensions;
    std::vector<FeatureStructRequest> m_DeviceFeatureStructs;

    VkPhysicalDeviceVulkan11Features m_Vulkan11Features { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    VkPhysicalDeviceVulkan12Features m_Vulkan12Features { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceVulkan13Features m_Vulkan13Features { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };

    uint32_t m_ExtraGraphicsQueues = 0;
    uint32_t m_ExtraComputeQueues = 0;

    bool b_FeaturesSatisfied = true;
  };
}
