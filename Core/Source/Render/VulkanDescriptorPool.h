#pragma once

namespace YAEngine
{
  class VulkanDescriptorPool
  {
  public:

    void Init(VkDevice device);
    void Destroy();

    VkDescriptorSet Allocate(VkDescriptorSetLayout layout);

    VkDescriptorPool Get() const
    {
      return m_Pools.empty() ? VK_NULL_HANDLE : m_Pools[0];
    }

  private:

    void CreatePool();

    std::vector<VkDescriptorPool> m_Pools;
    VkDevice m_Device {};
  };
}
