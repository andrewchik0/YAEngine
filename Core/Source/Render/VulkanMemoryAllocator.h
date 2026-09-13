#pragma once

#include "Pch.h"

namespace YAEngine
{
  class VulkanMemoryAllocator
  {
  public:

    // bufferDeviceAddress must match the core Vulkan 1.2 feature the device was created
    // with: the flag tags VMA's memory blocks with VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT so
    // a buffer the caller gave VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT can hand out its
    // address. Passing it without the feature enabled is invalid.
    void Init(VkInstance instance, VkDevice device, VkPhysicalDevice physicalDevice, bool bufferDeviceAddress);
    void Destroy();

    VmaAllocator& Get()
    {
      return m_Allocator;
    }

  private:

    VmaAllocator m_Allocator {};
  };
}