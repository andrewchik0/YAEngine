#pragma once

#include "VulkanDescriptorSet.h"
#include "VulkanUniformBuffer.h"

namespace YAEngine
{
  struct RenderContext;

  class PerFrameData
  {
  public:

    struct __PerFrameUBO
    {
      glm::mat4 view;
      glm::mat4 proj;
      glm::mat4 invProj;
      glm::mat4 prevView;
      glm::mat4 prevProj;
      glm::vec3 cameraPosition;
      float time;
      glm::vec3 cameraDirection;
      float gamma;
      float exposure;
      int currentTexture;
      float nearPlane;
      float farPlane;
      float fov;
      int screenWidth;
      int screenHeight;
    } ubo;

    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    void SetUp(uint32_t frameIndex);

    VkDescriptorSetLayout GetLayout()
    {
      return m_DescriptorSets[0].GetLayout();
    }

    VkDescriptorSet GetDescriptorSet(uint32_t frameIndex)
    {
      return m_DescriptorSets[frameIndex].Get();
    }

  private:

    std::vector<VulkanDescriptorSet> m_DescriptorSets;
    std::vector<VulkanUniformBuffer> m_UniformBuffers;
  };
}
