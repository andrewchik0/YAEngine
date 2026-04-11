#pragma once

#include "VulkanDescriptorSet.h"
#include "VulkanTexture.h"
#include "VulkanUniformBuffer.h"
#include "TerrainMaterialUniforms.h"

namespace YAEngine
{
  struct Material;
  struct RenderContext;
  struct TerrainMaterialComponent;
  class TextureManager;

  class VulkanTerrainMaterial
  {
  public:

    TerrainMaterialUniforms uniforms {};

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

    void Bind(TextureManager& textures, Material& layer0,
              const TerrainMaterialComponent& layer1,
              uint32_t currentFrame, const VulkanTexture& noneTexture);

  private:

    std::vector<VulkanDescriptorSet> m_DescriptorSets;
    std::vector<VulkanUniformBuffer> m_UniformBuffers;
    uint32_t m_BoundGeneration = UINT32_MAX;
    uint32_t m_LastFrame = UINT32_MAX;
  };
}
