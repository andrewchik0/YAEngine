#pragma once

#include "Pch.h"

namespace YAEngine
{
  class VulkanMemoryAllocator
  {
  public:

    void Init(VkInstance instance, VkDevice device, VkPhysicalDevice physicalDevice);
    void Destroy();

    VmaAllocator& Get()
    {
      return m_Allocator;
    }

  private:

    VmaAllocator m_Allocator {};
  };
}