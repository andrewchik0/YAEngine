#include "VulkanDescriptorPool.h"

#include "Utils/Log.h"

namespace YAEngine
{
  void VulkanDescriptorPool::Init(VkDevice device, bool accelerationStructures)
  {
    m_Device = device;
    b_AccelerationStructures = accelerationStructures;
    CreatePool();
  }

  void VulkanDescriptorPool::InitUpdateAfterBind(VkDevice device, uint32_t sampledImages)
  {
    m_Device = device;
    b_UpdateAfterBind = true;
    m_UpdateAfterBindImages = sampledImages;
    CreatePool();
  }

  void VulkanDescriptorPool::CreatePool()
  {
    std::vector<VkDescriptorPoolSize> poolSizes;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;

    if (b_UpdateAfterBind)
    {
      poolSizes.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_UpdateAfterBindImages });
      // Without this the layout, which carries UPDATE_AFTER_BIND_POOL, cannot be allocated
      // from here at all - the two flags are checked against each other at allocation.
      poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
      poolInfo.maxSets = 1;
    }
    else
    {
      poolSizes = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 500 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 200 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 50 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 50 }
      };

      if (b_AccelerationStructures)
        poolSizes.push_back({ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 16 });

      poolInfo.maxSets = 500;
    }

    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    VkDescriptorPool pool;
    if (vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create descriptor pool");
      throw std::runtime_error("failed to create descriptor pool!");
    }
    m_Pools.push_back(pool);
  }

  VkDescriptorSet VulkanDescriptorPool::Allocate(VkDescriptorSetLayout layout, uint32_t variableDescriptorCount)
  {
    VkDescriptorSetVariableDescriptorCountAllocateInfo variableInfo{};
    variableInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    variableInfo.descriptorSetCount = 1;
    variableInfo.pDescriptorCounts = &variableDescriptorCount;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_Pools.back();
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;
    if (variableDescriptorCount > 0)
      allocInfo.pNext = &variableInfo;

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
