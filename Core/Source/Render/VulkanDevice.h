#pragma once
#include "VulkanPhysicalDevice.h"

namespace YAEngine
{
  class VulkanInstance;
}

namespace YAEngine
{
  class VulkanDevice
  {
  public:

    void Init(VulkanInstance& instance, VulkanPhysicalDevice physicalDevice, VkSurfaceKHR& surface);
    void Destroy();

    VkDevice& Get()
    {
      return m_Device;
    }

  private:

    VkDevice m_Device {};
  };
}
