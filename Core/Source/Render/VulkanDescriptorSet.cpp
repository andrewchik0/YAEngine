#include "VulkanDescriptorSet.h"

namespace YAEngine
{
  void VulkanDescriptorSet::Init(VkDevice device, VkDescriptorPool descriptorPool, const SetDescription& setDescription)
  {
    m_Device = device;
    m_DescriptorPool = descriptorPool;
    b_OwnsLayout = true;

    std::vector<VkDescriptorSetLayoutBinding> vkBindings;
    vkBindings.reserve(setDescription.bindings.size());

    for (const BindingDescription& b : setDescription.bindings)
    {
      VkDescriptorSetLayoutBinding binding{};
      binding.binding = b.binding;
      binding.descriptorType = b.type;
      binding.descriptorCount = 1;
      binding.stageFlags = b.stages;
      binding.pImmutableSamplers = nullptr;

      vkBindings.push_back(binding);
    }

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<uint32_t>(vkBindings.size());
    info.pBindings = vkBindings.data();

    if (vkCreateDescriptorSetLayout(m_Device, &info, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create descriptor set layout!");
    }

    Init(device, descriptorPool, m_DescriptorSetLayout);
  }

  void VulkanDescriptorSet::Init(VkDevice device, VkDescriptorPool descriptorPool,
    VkDescriptorSetLayout descriptorSetLayout)
  {
    m_Device = device;
    m_DescriptorPool = descriptorPool;
    m_DescriptorSetLayout = descriptorSetLayout;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_DescriptorSetLayout;

    int result = 0;
    if ((result = vkAllocateDescriptorSets(m_Device, &allocInfo, &m_DescriptorSet)) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to allocate descriptor set!");
    }
  }

  void VulkanDescriptorSet::Destroy()
  {
    if (b_OwnsLayout)
    {
      vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
    }
  }

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
    VkImageLayout layout)
  {
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = imageView;
    imageInfo.sampler   = sampler;
    imageInfo.imageLayout = layout;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_DescriptorSet;
    write.dstBinding = binding;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
  }

}
