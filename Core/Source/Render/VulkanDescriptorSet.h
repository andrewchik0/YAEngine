#pragma once

#include "Pch.h"

namespace YAEngine
{
  struct BindingDescription
  {
    uint32_t binding;
    VkDescriptorType type;
    VkShaderStageFlags stages;
  };

  struct SetDescription
  {
    uint32_t set;
    std::vector<BindingDescription> bindings;
  };

  class VulkanDescriptorSet
  {
  public:

    void Init(VkDevice device, VkDescriptorPool descriptorPool, const SetDescription& setDescription);
    void Init(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout);
    void Destroy();

    void WriteUniformBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size);
    void WriteStorageBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size);
    void WriteCombinedImageSampler(
      uint32_t binding,
      VkImageView imageView,
      VkSampler sampler,
      VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    VkDescriptorSet Get()
    {
      return m_DescriptorSet;
    }

    VkDescriptorSetLayout GetLayout() const
    {
      return m_DescriptorSetLayout;
    }

  private:

    VkDevice m_Device {};
    VkDescriptorSetLayout m_DescriptorSetLayout {};
    VkDescriptorSet m_DescriptorSet {};
    VkDescriptorPool m_DescriptorPool {};

    bool b_OwnsLayout = false;
  };
}