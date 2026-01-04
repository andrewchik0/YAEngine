#pragma once

namespace YAEngine
{
  class VulkanDescriptorPool
  {
  public:

    void Init(VkDevice device);
    void Destroy();

    VkDescriptorPool Get()
    {
      return m_DescriptorPool;
    }

  private:

    VkDescriptorPool m_DescriptorPool {};
    VkDevice m_Device {};
  };
}