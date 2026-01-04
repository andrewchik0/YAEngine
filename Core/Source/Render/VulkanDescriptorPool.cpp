#include "VulkanDescriptorPool.h"

namespace YAEngine
{
  void VulkanDescriptorPool::Init(VkDevice device)
  {
    m_Device = device;
    std::vector<VkDescriptorPoolSize> poolSizes = {
      { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 },
      { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 },
      { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 50 }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 100;

    if (vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create descriptor pool!");
    }
  }

  void VulkanDescriptorPool::Destroy()
  {
    vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
  }
}
