#pragma once

#include "Pch.h"

namespace YAEngine
{
  struct RenderContext;

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

    void Init(const RenderContext& ctx, const SetDescription& setDescription);
    void Init(const RenderContext& ctx, VkDescriptorSetLayout descriptorSetLayout);
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

    bool b_OwnsLayout = false;
  };
}
