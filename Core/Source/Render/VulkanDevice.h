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

    bool IsMultiDrawIndirectSupported() const
    {
      return b_MultiDrawIndirectSupported;
    }

    bool IsDrawIndirectFirstInstanceSupported() const
    {
      return b_DrawIndirectFirstInstanceSupported;
    }

    bool IsUnorm16VertexSupported() const
    {
      return b_Unorm16VertexSupported;
    }

  private:

    VkDevice m_Device {};
    bool b_DepthClampSupported = false;
    bool b_MultiDrawIndirectSupported = false;
    bool b_DrawIndirectFirstInstanceSupported = false;
    bool b_Unorm16VertexSupported = false;
  };
}
