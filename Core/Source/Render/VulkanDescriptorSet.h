#pragma once

#include "Pch.h"

namespace YAEngine
{
  struct RenderContext;
  class VulkanDescriptorPool;

  struct BindingDescription
  {
    uint32_t binding;
    VkDescriptorType type;
    VkShaderStageFlags stages;
    // Array size of the binding. Everything but a bindless table leaves it at one.
    uint32_t count = 1;
    // VK_DESCRIPTOR_BINDING_*_BIT. A layout carrying UPDATE_AFTER_BIND on any binding is
    // created with UPDATE_AFTER_BIND_POOL and may only be allocated from a pool that
    // carries the matching flag; VARIABLE_DESCRIPTOR_COUNT is legal on the last binding
    // only, and its real size is given at allocation time.
    VkDescriptorBindingFlags flags = 0;
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
      VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      uint32_t arrayElement = 0
    );
    DescriptorWriter& WriteStorageImage(
      uint32_t binding,
      VkImageView imageView,
      VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL
    );
    // An acceleration structure has no VkDescriptorImageInfo or BufferInfo of its own: the
    // handle travels in a struct chained onto the write through pNext.
    DescriptorWriter& WriteAccelerationStructure(
      uint32_t binding,
      VkAccelerationStructureKHR structure
    );
    void Flush();

  private:

    // The path tracer's set is the largest writer at 25 bindings. Every Write* adds exactly one
    // write and at most one info, so ReserveWrite guarding the write count guards all arrays.
    static constexpr uint32_t MAX_WRITES = 32;

    void ReserveWrite(uint32_t binding) const;

    VkDevice m_Device {};
    VkDescriptorSet m_Set {};

    uint32_t m_WriteCount = 0;
    uint32_t m_BufferInfoCount = 0;
    uint32_t m_ImageInfoCount = 0;
    uint32_t m_AccelInfoCount = 0;

    std::array<VkWriteDescriptorSet, MAX_WRITES> m_Writes {};
    std::array<VkDescriptorBufferInfo, MAX_WRITES> m_BufferInfos {};
    std::array<VkDescriptorImageInfo, MAX_WRITES> m_ImageInfos {};
    std::array<VkWriteDescriptorSetAccelerationStructureKHR, MAX_WRITES> m_AccelInfos {};
    // pAccelerationStructures points at an ARRAY of handles, so the handle needs storage
    // that outlives the call queueing the write just as much as the struct above does.
    std::array<VkAccelerationStructureKHR, MAX_WRITES> m_AccelHandles {};
  };

  class VulkanDescriptorSet
  {
  public:

    void Init(const RenderContext& ctx, const SetDescription& setDescription);
    void Init(const RenderContext& ctx, VkDescriptorSetLayout descriptorSetLayout);
    // Allocates from `pool` instead of the context's shared one, with the real size of a
    // VARIABLE_DESCRIPTOR_COUNT last binding. An update-after-bind layout may only come
    // from a pool created for it, which the shared pool is not.
    void Init(const RenderContext& ctx, const SetDescription& setDescription,
      VulkanDescriptorPool& pool, uint32_t variableDescriptorCount);
    void Destroy();

    void WriteUniformBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size);
    void WriteStorageBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size);
    void WriteCombinedImageSampler(
      uint32_t binding,
      VkImageView imageView,
      VkSampler sampler,
      VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      uint32_t arrayElement = 0
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

    // Fills m_DescriptorSetLayout from the cache, or creates and owns one when the context
    // carries none. Shared by every Init that takes a SetDescription.
    void CreateLayout(const RenderContext& ctx, const SetDescription& setDescription);

    VkDevice m_Device {};
    VkDescriptorSetLayout m_DescriptorSetLayout {};
    VkDescriptorSet m_DescriptorSet {};

    bool b_OwnsLayout = false;
  };
}
