#include "VulkanDescriptorPool.h"

#include "Log.h"

namespace YAEngine
{
  void VulkanDescriptorPool::Init(VkDevice device)
  {
    m_Device = device;
    CreatePool();
  }

  void VulkanDescriptorPool::CreatePool()
  {
    std::vector<VkDescriptorPoolSize> poolSizes = {
      { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 500 },
      { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 200 },
      { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 50 }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 500;

    VkDescriptorPool pool;
    if (vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create descriptor pool");
      throw std::runtime_error("failed to create descriptor pool!");
    }
    m_Pools.push_back(pool);
  }

  VkDescriptorSet VulkanDescriptorPool::Allocate(VkDescriptorSetLayout layout)
  {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_Pools.back();
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet set;
    VkResult result = vkAllocateDescriptorSets(m_Device, &allocInfo, &set);

    if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL)
    {
      CreatePool();
      allocInfo.descriptorPool = m_Pools.back();
      if (vkAllocateDescriptorSets(m_Device, &allocInfo, &set) != VK_SUCCESS)
      {
        YA_LOG_ERROR("Render", "Failed to allocate descriptor set from new pool");
        throw std::runtime_error("failed to allocate descriptor set from new pool!");
      }
    }
    else if (result != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to allocate descriptor set, VkResult = %d", result);
      throw std::runtime_error("failed to allocate descriptor set!");
    }

    return set;
  }

  void VulkanDescriptorPool::Destroy()
  {
    for (auto pool : m_Pools)
    {
      vkDestroyDescriptorPool(m_Device, pool, nullptr);
    }
    m_Pools.clear();
  }
}
