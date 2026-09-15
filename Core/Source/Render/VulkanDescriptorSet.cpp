#include "VulkanDescriptorSet.h"

#include "DescriptorLayoutCache.h"
#include "RenderContext.h"
#include "VulkanDescriptorPool.h"
#include "Utils/Log.h"

namespace YAEngine
{
  void VulkanDescriptorSet::CreateLayout(const RenderContext& ctx, const SetDescription& setDescription)
  {
    m_Device = ctx.device;

    if (ctx.layoutCache)
    {
      m_DescriptorSetLayout = ctx.layoutCache->GetOrCreate(ctx.device, setDescription.bindings);
      b_OwnsLayout = false;
      return;
    }

    b_OwnsLayout = true;

    std::vector<VkDescriptorSetLayoutBinding> vkBindings;
    std::vector<VkDescriptorBindingFlags> bindingFlags;
    vkBindings.reserve(setDescription.bindings.size());
    bindingFlags.reserve(setDescription.bindings.size());

    VkDescriptorBindingFlags anyFlags = 0;
    for (const BindingDescription& b : setDescription.bindings)
    {
      VkDescriptorSetLayoutBinding binding{};
      binding.binding = b.binding;
      binding.descriptorType = b.type;
      binding.descriptorCount = b.count;
      binding.stageFlags = b.stages;
      binding.pImmutableSamplers = nullptr;

      vkBindings.push_back(binding);
      bindingFlags.push_back(b.flags);
      anyFlags |= b.flags;
    }

    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    flagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
    flagsInfo.pBindingFlags = bindingFlags.data();

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<uint32_t>(vkBindings.size());
    info.pBindings = vkBindings.data();
    if (anyFlags != 0)
    {
      info.pNext = &flagsInfo;
      // Required of the layout as soon as one of its bindings is update-after-bind, and it
      // is what ties the layout to a pool created with the matching flag.
      if ((anyFlags & VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT) != 0)
        info.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    }

    if (vkCreateDescriptorSetLayout(m_Device, &info, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create descriptor set layout");
      throw std::runtime_error("failed to create descriptor set layout!");
    }
  }

  void VulkanDescriptorSet::Init(const RenderContext& ctx, const SetDescription& setDescription)
  {
    CreateLayout(ctx, setDescription);
    Init(ctx, m_DescriptorSetLayout);
  }

  void VulkanDescriptorSet::Init(const RenderContext& ctx, const SetDescription& setDescription,
    VulkanDescriptorPool& pool, uint32_t variableDescriptorCount)
  {
    CreateLayout(ctx, setDescription);
    m_DescriptorSet = pool.Allocate(m_DescriptorSetLayout, variableDescriptorCount);
  }

  void VulkanDescriptorSet::Init(const RenderContext& ctx, VkDescriptorSetLayout descriptorSetLayout)
  {
    m_Device = ctx.device;
    m_DescriptorSetLayout = descriptorSetLayout;

    if (ctx.descriptorPool)
    {
      m_DescriptorSet = ctx.descriptorPool->Allocate(descriptorSetLayout);
    }
    else
    {
      YA_LOG_ERROR("Render", "Descriptor pool is null in RenderContext");
      throw std::runtime_error("descriptor pool is null in RenderContext!");
    }
  }

  void VulkanDescriptorSet::Destroy()
  {
    if (b_OwnsLayout)
    {
      vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
    }
  }

  void DescriptorWriter::ReserveWrite(uint32_t binding) const
  {
    if (m_WriteCount >= MAX_WRITES)
    {
      YA_LOG_ERROR("Render", "DescriptorWriter overflow: binding %u exceeds %u queued writes",
        binding, MAX_WRITES);
      throw std::runtime_error("DescriptorWriter overflow");
    }
  }

  DescriptorWriter& DescriptorWriter::WriteUniformBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size)
  {
    ReserveWrite(binding);
    auto& bufferInfo = m_BufferInfos[m_BufferInfoCount++];
    bufferInfo = {};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = 0;
    bufferInfo.range  = size;

    auto& write = m_Writes[m_WriteCount++];
    write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_Set;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;
    return *this;
  }

  DescriptorWriter& DescriptorWriter::WriteStorageBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size)
  {
    ReserveWrite(binding);
    auto& bufferInfo = m_BufferInfos[m_BufferInfoCount++];
    bufferInfo = {};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = 0;
    bufferInfo.range  = size;

    auto& write = m_Writes[m_WriteCount++];
    write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_Set;
    write.dstBinding = binding;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;
    return *this;
  }

  DescriptorWriter& DescriptorWriter::WriteCombinedImageSampler(
    uint32_t binding,
    VkImageView imageView,
    VkSampler sampler,
    VkImageLayout layout,
    uint32_t arrayElement)
  {
    ReserveWrite(binding);
    auto& imageInfo = m_ImageInfos[m_ImageInfoCount++];
    imageInfo = {};
    imageInfo.imageView = imageView;
    imageInfo.sampler   = sampler;
    imageInfo.imageLayout = layout;

    auto& write = m_Writes[m_WriteCount++];
    write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_Set;
    write.dstBinding = binding;
    write.dstArrayElement = arrayElement;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
    return *this;
  }

  DescriptorWriter& DescriptorWriter::WriteStorageImage(
    uint32_t binding,
    VkImageView imageView,
    VkImageLayout layout)
  {
    ReserveWrite(binding);
    auto& imageInfo = m_ImageInfos[m_ImageInfoCount++];
    imageInfo = {};
    imageInfo.imageView = imageView;
    imageInfo.sampler   = VK_NULL_HANDLE;
    imageInfo.imageLayout = layout;

    auto& write = m_Writes[m_WriteCount++];
    write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_Set;
    write.dstBinding = binding;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
    return *this;
  }

  DescriptorWriter& DescriptorWriter::WriteAccelerationStructure(
    uint32_t binding,
    VkAccelerationStructureKHR structure)
  {
    ReserveWrite(binding);
    auto& handle = m_AccelHandles[m_AccelInfoCount];
    handle = structure;

    auto& accelInfo = m_AccelInfos[m_AccelInfoCount++];
    accelInfo = {};
    accelInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    accelInfo.accelerationStructureCount = 1;
    accelInfo.pAccelerationStructures = &handle;

    auto& write = m_Writes[m_WriteCount++];
    write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.pNext = &accelInfo;
    write.dstSet = m_Set;
    write.dstBinding = binding;
    write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    write.descriptorCount = 1;
    return *this;
  }

  void DescriptorWriter::Flush()
  {
    if (m_WriteCount > 0)
    {
      vkUpdateDescriptorSets(m_Device, m_WriteCount, m_Writes.data(), 0, nullptr);
      m_WriteCount = 0;
      m_BufferInfoCount = 0;
      m_ImageInfoCount = 0;
      m_AccelInfoCount = 0;
    }
  }

  // VulkanDescriptorSet Write* methods (immediate, backward-compatible)

  void VulkanDescriptorSet::WriteUniformBuffer(
    uint32_t binding,
    VkBuffer buffer,
    VkDeviceSize size)
  {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = 0;
    bufferInfo.range  = size;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_DescriptorSet;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
  }

  void VulkanDescriptorSet::WriteStorageBuffer(
    uint32_t binding,
    VkBuffer buffer,
    VkDeviceSize size)
  {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = 0;
    bufferInfo.range  = size;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_DescriptorSet;
    write.dstBinding = binding;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
  }

  void VulkanDescriptorSet::WriteCombinedImageSampler(
    uint32_t binding,
    VkImageView imageView,
    VkSampler sampler,
    VkImageLayout layout,
    uint32_t arrayElement)
  {
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = imageView;
    imageInfo.sampler   = sampler;
    imageInfo.imageLayout = layout;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_DescriptorSet;
    write.dstBinding = binding;
    write.dstArrayElement = arrayElement;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
  }

  void VulkanDescriptorSet::WriteStorageImage(
    uint32_t binding,
    VkImageView imageView,
    VkImageLayout layout)
  {
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = imageView;
    imageInfo.sampler   = VK_NULL_HANDLE;
    imageInfo.imageLayout = layout;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_DescriptorSet;
    write.dstBinding = binding;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
  }

}
