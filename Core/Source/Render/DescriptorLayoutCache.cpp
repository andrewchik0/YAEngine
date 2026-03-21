#include "DescriptorLayoutCache.h"

#include <algorithm>

namespace YAEngine
{
  bool DescriptorLayoutCache::LayoutKey::operator==(const LayoutKey& other) const
  {
    if (bindings.size() != other.bindings.size()) return false;
    for (size_t i = 0; i < bindings.size(); i++)
    {
      if (bindings[i].binding != other.bindings[i].binding) return false;
      if (bindings[i].type != other.bindings[i].type) return false;
      if (bindings[i].stages != other.bindings[i].stages) return false;
    }
    return true;
  }

  size_t DescriptorLayoutCache::LayoutKeyHash::operator()(const LayoutKey& key) const
  {
    size_t hash = key.bindings.size();
    for (const auto& b : key.bindings)
    {
      hash ^= std::hash<uint32_t>{}(b.binding) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
      hash ^= std::hash<uint32_t>{}(static_cast<uint32_t>(b.type)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
      hash ^= std::hash<uint32_t>{}(static_cast<uint32_t>(b.stages)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }
    return hash;
  }

  VkDescriptorSetLayout DescriptorLayoutCache::GetOrCreate(VkDevice device, const std::vector<BindingDescription>& bindings)
  {
    LayoutKey key;
    key.bindings = bindings;
    std::sort(key.bindings.begin(), key.bindings.end(), [](const BindingDescription& a, const BindingDescription& b)
    {
      return a.binding < b.binding;
    });

    auto it = m_Cache.find(key);
    if (it != m_Cache.end())
      return it->second;

    std::vector<VkDescriptorSetLayoutBinding> vkBindings;
    vkBindings.reserve(bindings.size());

    for (const BindingDescription& b : bindings)
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

    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create descriptor set layout!");
    }

    m_Cache[key] = layout;
    return layout;
  }

  void DescriptorLayoutCache::Destroy(VkDevice device)
  {
    for (auto& [key, layout] : m_Cache)
    {
      vkDestroyDescriptorSetLayout(device, layout, nullptr);
    }
    m_Cache.clear();
  }
}
