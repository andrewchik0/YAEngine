#pragma once

#include "VulkanDescriptorSet.h"

namespace YAEngine
{
  class DescriptorLayoutCache
  {
  public:

    VkDescriptorSetLayout GetOrCreate(VkDevice device, const std::vector<BindingDescription>& bindings);
    void Destroy(VkDevice device);

  private:

    struct LayoutKey
    {
      std::vector<BindingDescription> bindings;

      bool operator==(const LayoutKey& other) const;
    };

    struct LayoutKeyHash
    {
      size_t operator()(const LayoutKey& key) const;
    };

    std::unordered_map<LayoutKey, VkDescriptorSetLayout, LayoutKeyHash> m_Cache;
  };
}
