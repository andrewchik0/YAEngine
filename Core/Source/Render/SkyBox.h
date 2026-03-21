#pragma once

#include "VulkanDescriptorSet.h"
#include "VulkanPipeline.h"

namespace YAEngine
{
  struct RenderContext;
  struct CubicTextureResources;
  class VulkanCubicTexture;

  class SkyBox
  {
  public:

    void Init(const RenderContext& ctx, VkRenderPass renderPass);
    void Draw(uint32_t currentFrame, VulkanCubicTexture* cube, VkCommandBuffer commandBuffer, glm::mat4& camDir, glm::mat4& proj, const CubicTextureResources& res);
    void Destroy();

  private:

    VulkanPipeline m_Pipeline;
    std::vector<VulkanDescriptorSet> m_DescriptorSets;
  };
}
