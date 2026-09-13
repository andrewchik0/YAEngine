#pragma once

namespace YAEngine
{
  class VulkanDescriptorPool
  {
  public:

    // accelerationStructures adds a pool size for VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR.
    // It is a request for a type the device only understands with VK_KHR_acceleration_structure
    // enabled, so it is not asked for unconditionally.
    void Init(VkDevice device, bool accelerationStructures);

    // A pool that backs a single update-after-bind set of `sampledImages` combined image
    // samplers - the bindless texture table. Kept apart from the shared pool above for two
    // reasons: UPDATE_AFTER_BIND_POOL_BIT applies to every set a pool hands out, and the
    // table alone asks for an order of magnitude more descriptors than the rest of the
    // engine put together.
    void InitUpdateAfterBind(VkDevice device, uint32_t sampledImages);

    void Destroy();

    // variableDescriptorCount is the real size of a layout's VARIABLE_DESCRIPTOR_COUNT
    // last binding. Zero means the layout has none.
    VkDescriptorSet Allocate(VkDescriptorSetLayout layout, uint32_t variableDescriptorCount = 0);

    VkDescriptorPool Get() const
    {
      return m_Pools.empty() ? VK_NULL_HANDLE : m_Pools[0];
    }

  private:

    void CreatePool();

    std::vector<VkDescriptorPool> m_Pools;
    VkDevice m_Device {};
    bool b_AccelerationStructures = false;
    // Set by InitUpdateAfterBind. The pool then holds exactly one set of
    // m_UpdateAfterBindImages combined image samplers and nothing else.
    bool b_UpdateAfterBind = false;
    uint32_t m_UpdateAfterBindImages = 0;
  };
}
