#pragma once

#include "VulkanDescriptorSet.h"
#include "VulkanTexture.h"
#include "VulkanUniformBuffer.h"
#include "MaterialUniforms.h"

namespace YAEngine
{
  struct Material;
  struct RenderContext;

  class VulkanMaterial
  {
  public:

    MaterialUniforms uniforms;

    void Init(const RenderContext& ctx, const VulkanTexture& noneTexture);
    void Destroy(const RenderContext& ctx);

    VkDescriptorSetLayout GetLayout()
    {
      return m_DescriptorSets[0].GetLayout();
    }

    VkDescriptorSet GetDescriptorSet(uint32_t currentFrame)
    {
      return m_DescriptorSets[currentFrame].Get();
    }

    void Bind(Application* app, Material& material, uint32_t currentFrame, const VulkanTexture& noneTexture);

  private:

    std::vector<VulkanDescriptorSet> m_DescriptorSets;
    std::vector<VulkanUniformBuffer> m_UniformBuffers;
    std::vector<uint32_t> m_BoundGenerations;
  };
}
