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

    bool IsDepthClampSupported() const
    {
      return b_DepthClampSupported;
    }

  private:

    VkDevice m_Device {};
    bool b_DepthClampSupported = false;
  };
}
