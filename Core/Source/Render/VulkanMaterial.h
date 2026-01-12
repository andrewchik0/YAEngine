#pragma once
#include "VulkanDescriptorSet.h"
#include "VulkanTexture.h"
#include "VulkanUniformBuffer.h"

namespace YAEngine
{
  struct Material;

  class VulkanMaterial
  {
  public:

    struct __PerMaterialData
    {
      glm::vec3 albedo;
      float roughness;
      glm::vec3 emissivity;
      float specular;
      float metallic;
      int textureMask;
      int sg;
    } data;

    static void InitMaterials(VkDevice device, VkDescriptorPool pool, VmaAllocator allocator, uint32_t maxFramesInFlight);
    void Init();
    void Destroy();

    VkDescriptorSetLayout GetLayout()
    {
      return m_DescriptorSets[0].GetLayout();
    }

    VkDescriptorSet GetDescriptorSet(uint32_t currentFrame)
    {
      return m_DescriptorSets[currentFrame].Get();
    }

    void Bind(Application* app, Material& material, uint32_t currentFrame);

  private:

    std::vector<VulkanDescriptorSet> m_DescriptorSets;
    std::vector<VulkanUniformBuffer> m_UniformBuffers;

    static uint32_t s_MaxFramesInFlight;
    static VkDevice s_Device;
    static VkDescriptorPool s_Pool;
    static VmaAllocator s_Allocator;
    static VulkanTexture s_NoneTexture;
  };
}
