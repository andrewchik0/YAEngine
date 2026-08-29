#pragma once
#include "VulkanPhysicalDevice.h"

namespace YAEngine
{
  class VulkanInstance;
}

namespace YAEngine
{
  struct VulkanQueueLocation
  {
    uint32_t family = 0;
    uint32_t index = 0;
  };

  class VulkanDevice
  {
  public:

    void Init(VulkanInstance& instance, VulkanPhysicalDevice physicalDevice, VkSurfaceKHR& surface,
              VulkanRequirements& requirements);
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

    VulkanQueueLocation GetGraphicsQueueLocation() const
    {
      return m_GraphicsQueue;
    }

    const std::vector<VulkanQueueLocation>& GetExtraGraphicsQueues() const
    {
      return m_ExtraGraphicsQueues;
    }

    const std::vector<VulkanQueueLocation>& GetExtraComputeQueues() const
    {
      return m_ExtraComputeQueues;
    }

  private:

    VkDevice m_Device {};
    bool b_DepthClampSupported = false;
    bool b_MultiDrawIndirectSupported = false;
    bool b_DrawIndirectFirstInstanceSupported = false;
    bool b_Unorm16VertexSupported = false;

    VulkanQueueLocation m_GraphicsQueue {};
    std::vector<VulkanQueueLocation> m_ExtraGraphicsQueues;
    std::vector<VulkanQueueLocation> m_ExtraComputeQueues;
  };
}
