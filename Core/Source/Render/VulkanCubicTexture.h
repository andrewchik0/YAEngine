#pragma once

#include "VulkanTexture.h"
#include "VulkanImage.h"

namespace YAEngine
{
  struct RenderContext;

  static constexpr uint32_t CUBEMAP_SIZE = 1024;

  namespace Detail
  {
    constexpr uint32_t MipLevels(uint32_t size)
    {
      uint32_t levels = 1;
      while (size >>= 1) ++levels;
      return levels;
    }
  }

  static constexpr uint32_t CUBEMAP_MAX_MIP_LEVELS = Detail::MipLevels(CUBEMAP_SIZE);

  struct CubicTextureResources
  {
    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    glm::mat4 views[6] {};
    glm::mat4 projection {};
    VkBuffer vertexBuffer {};
    VmaAllocation vertexBufferAllocation {};

    VkRenderPass renderPass {};
    VkDescriptorSetLayout descriptorSetLayout {};
    VkPipelineLayout pipelineLayout {};
    VkPipeline pipeline {};

    VkRenderPass irradianceRenderPass {};
    VkDescriptorSetLayout irradianceDescriptorSetLayout {};
    VkPipelineLayout irradiancePipelineLayout {};
    VkPipeline irradiancePipeline {};

    VkRenderPass prefilterRenderPass {};
    VkDescriptorSetLayout prefilterDescriptorSetLayout {};
    VkPipelineLayout prefilterPipelineLayout {};
    VkPipeline prefilterPipeline {};

    VulkanTexture brdfLut;

  private:
    void InitRenderPass(VkDevice device, VkRenderPass& outRenderPass, const char* debugName);
    void InitPipeline(VkDevice device, VkRenderPass rp, const char* fragShader,
                      uint32_t pushConstantSize, VkShaderStageFlags pushStages,
                      VkDescriptorSetLayout& outSetLayout, VkPipelineLayout& outPipelineLayout,
                      VkPipeline& outPipeline, const char* debugName);
    void CreateVertexBuffer(const RenderContext& ctx);
  };

  class VulkanCubicTexture
  {
  public:

    void Create(const RenderContext& ctx, CubicTextureResources& res, void* data, uint32_t width, uint32_t height);
    void Destroy(const RenderContext& ctx);

    VkImageView GetView() { return m_CubemapImageView; }
    VkImage GetImage() { return m_CubemapImage; }
    VkSampler GetSampler() { return m_CubeMapSampler; }

    VkImageView GetIrradianceView() { return m_Irradiance.GetView(); }
    VkImage GetIrradianceImage() { return m_Irradiance.GetImage(); }
    VkSampler GetIrradianceSampler() { return m_Irradiance.GetSampler(); }

    VkImageView GetPrefilterView() { return m_Prefilter.GetView(); }
    VkImage GetPrefilterImage() { return m_Prefilter.GetImage(); }
    VkSampler GetPrefilterSampler() { return m_Prefilter.GetSampler(); }

    static void DrawCube(VkCommandBuffer cmd, const CubicTextureResources& res);

  private:

    VulkanTexture m_EquirectTexture;

    VkImage m_CubemapImage {};
    VkImageView m_CubemapImageView {};
    VmaAllocation m_CubemapImageAllocation {};
    VkSampler m_CubeMapSampler {};
    VkImageView m_FaceViews[6] {};
    VkFramebuffer m_FrameBuffers[6] {};

    VulkanImage m_Irradiance;
    VulkanImage m_Prefilter;
  };
}
