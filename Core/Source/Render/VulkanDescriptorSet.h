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

  class DescriptorWriter
  {
  public:

    DescriptorWriter(VkDevice device, VkDescriptorSet set)
      : m_Device(device), m_Set(set) {}

    DescriptorWriter& WriteUniformBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size);
    DescriptorWriter& WriteStorageBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size);
    DescriptorWriter& WriteCombinedImageSampler(
      uint32_t binding,
      VkImageView imageView,
      VkSampler sampler,
      VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    DescriptorWriter& WriteStorageImage(
      uint32_t binding,
      VkImageView imageView,
      VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL
    );
    void Flush();

  private:

    static constexpr uint32_t MAX_WRITES = 16;

    VkDevice m_Device {};
    VkDescriptorSet m_Set {};

    uint32_t m_WriteCount = 0;
    uint32_t m_BufferInfoCount = 0;
    uint32_t m_ImageInfoCount = 0;

    std::array<VkWriteDescriptorSet, MAX_WRITES> m_Writes {};
    std::array<VkDescriptorBufferInfo, MAX_WRITES> m_BufferInfos {};
    std::array<VkDescriptorImageInfo, MAX_WRITES> m_ImageInfos {};
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
    void WriteStorageImage(
      uint32_t binding,
      VkImageView imageView,
      VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL
    );

    DescriptorWriter Writer() { return DescriptorWriter(m_Device, m_DescriptorSet); }

    VkDescriptorSet Get() const
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
