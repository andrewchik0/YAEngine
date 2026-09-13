#include "DescriptorLayoutCache.h"

#include "Utils/Log.h"

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
      if (bindings[i].count != other.bindings[i].count) return false;
      if (bindings[i].flags != other.bindings[i].flags) return false;
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
      hash ^= std::hash<uint32_t>{}(b.count) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
      hash ^= std::hash<uint32_t>{}(static_cast<uint32_t>(b.flags)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
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
    std::vector<VkDescriptorBindingFlags> bindingFlags;
    vkBindings.reserve(key.bindings.size());
    bindingFlags.reserve(key.bindings.size());

    VkDescriptorBindingFlags anyFlags = 0;
    for (const BindingDescription& b : key.bindings)
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

    // The two arrays are index-parallel, which is why the flags are gathered in the same
    // pass the bindings are: pBindingFlags[i] belongs to pBindings[i], not to a binding
    // number, and the sort above has already reordered both.
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

    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create descriptor set layout");
      throw std::runtime_error("failed to create descriptor set layout!");
    }

    m_Cache.emplace(std::move(key), layout);
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
