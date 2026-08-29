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
      bool enabled = false;
    };

    void AddInstanceExtension(const char* name);
    void AddDeviceExtension(const char* name);

    // Callers set the individual VkBool32 fields they need to VK_TRUE. Bits the device
    // does not support are cleared at device creation time.
    VkPhysicalDeviceVulkan11Features& GetVulkan11Features() { return m_Vulkan11Features; }
    VkPhysicalDeviceVulkan12Features& GetVulkan12Features() { return m_Vulkan12Features; }
    VkPhysicalDeviceVulkan13Features& GetVulkan13Features() { return m_Vulkan13Features; }

    void MergeVulkan11Features(const VkPhysicalDeviceVulkan11Features& features);
    void MergeVulkan12Features(const VkPhysicalDeviceVulkan12Features& features);
    void MergeVulkan13Features(const VkPhysicalDeviceVulkan13Features& features);

    // Clears every requested bit the device does not expose and returns how many were
    // dropped, so an unsupported optional feature costs a warning instead of a failure.
    uint32_t DropUnsupportedFeatures(const VkPhysicalDeviceVulkan11Features& supported11,
                                     const VkPhysicalDeviceVulkan12Features& supported12,
                                     const VkPhysicalDeviceVulkan13Features& supported13);

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

    VkPhysicalDeviceVulkan11Features m_Vulkan11Features { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    VkPhysicalDeviceVulkan12Features m_Vulkan12Features { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceVulkan13Features m_Vulkan13Features { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };

    uint32_t m_ExtraGraphicsQueues = 0;
    uint32_t m_ExtraComputeQueues = 0;

    bool b_FeaturesSatisfied = true;
  };
}
