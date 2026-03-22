#pragma once

#include "VulkanTexture.h"

namespace YAEngine
{
  struct RenderContext;

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
    void InitRenderPass(VkDevice device);
    void InitPipeline(VkDevice device);
    void InitIrradianceRenderPass(VkDevice device);
    void InitIrradiancePipeline(VkDevice device);
    void InitPrefilterRenderPass(VkDevice device);
    void InitPrefilterPipeline(VkDevice device);
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

    VkImageView GetIrradianceView() { return m_IrradianceImageView; }
    VkImage GetIrradianceImage() { return m_IrradianceImage; }
    VkSampler GetIrradianceSampler() { return m_IrradianceSampler; }

    VkImageView GetPrefilterView() { return m_PrefilterImageView; }
    VkImage GetPrefilterImage() { return m_PrefilterImage; }
    VkSampler GetPrefilterSampler() { return m_PrefilterSampler; }

    static void DrawCube(VkCommandBuffer cmd, const CubicTextureResources& res);

  private:

    VulkanTexture m_EquirectTexture;

    VkImage m_CubemapImage {};
    VkImageView m_CubemapImageView {};
    VmaAllocation m_CubemapImageAllocation {};
    VkSampler m_CubeMapSampler {};
    VkImageView m_FaceViews[6] {};
    VkFramebuffer m_FrameBuffers[6] {};

    VkImage m_IrradianceImage {};
    VkImageView m_IrradianceImageView {};
    VmaAllocation m_IrradianceImageAllocation {};
    VkSampler m_IrradianceSampler {};
    VkImageView m_IrradianceFaceViews[6] {};
    VkFramebuffer m_IrradianceFrameBuffers[6] {};

    VkImage m_PrefilterImage {};
    VkImageView m_PrefilterImageView {};
    VmaAllocation m_PrefilterImageAllocation {};
    VkSampler m_PrefilterSampler {};
    VkImageView m_PrefilterFaceViews[6 * 10] {};
    VkFramebuffer m_PrefilterFrameBuffers[6 * 10] {};

    void ComputeIrradiance(const RenderContext& ctx, CubicTextureResources& res);
    void ComputePrefilter(const RenderContext& ctx, CubicTextureResources& res);
  };
}
