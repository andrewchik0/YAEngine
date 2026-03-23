#define VMA_IMPLEMENTATION
#define VMA_VULKAN_VERSION 1003000
#include "VulkanMemoryAllocator/vk_mem_alloc.h"

#include "VulkanMemoryAllocator.h"

#include "Log.h"

namespace YAEngine
{
  void VulkanMemoryAllocator::Init(VkInstance instance, VkDevice device, VkPhysicalDevice physicalDevice)
  {
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;

    VkResult result = vmaCreateAllocator(&allocatorInfo, &m_Allocator);
    if (result != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create VMA allocator");
      throw std::runtime_error("Failed to create VMA allocator!");
    }

  }

  void VulkanMemoryAllocator::Destroy()
  {
    VmaTotalStatistics stats{};
    vmaCalculateStatistics(m_Allocator, &stats);

    char* statsStr = nullptr;
    vmaBuildStatsString(m_Allocator, &statsStr, VK_TRUE);

    YA_LOG_INFO("VMA", "Allocations alive: %u", stats.total.statistics.allocationCount);

    vmaFreeStatsString(m_Allocator, statsStr);
    vmaDestroyAllocator(m_Allocator);
  }
}
