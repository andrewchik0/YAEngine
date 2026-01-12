#pragma once
#include "VulkanRenderPass.h"
#include "VulkanTexture.h"

namespace YAEngine
{
  class VulkanCubicTexture
  {
  public:

    void Create(void* data, uint32_t width, uint32_t height);
    void Destroy();

    static void InitCubicTextures(VkDevice device, VmaAllocator allocator, VulkanCommandBuffer& commandBuffer, VkDescriptorPool descriptorPool);
    static void DestroyCubicTextures();

    VkImageView GetView()
    {
      return m_CubemapImageView;
    }

    VkImage GetImage()
    {
      return m_CubemapImage;
    }

    VkSampler GetSampler()
    {
      return m_CubeMapSampler;
    }

    VkImageView GetIrradianceView()
    {
      return m_IrradianceImageView;
    }

    VkImage GetIrradianceImage()
    {
      return m_IrradianceImage;
    }

    VkSampler GetIrradianceSampler()
    {
      return m_IrradianceSampler;
    }

    static void DrawCube(VkCommandBuffer cmd);

    static VulkanTexture m_BRDFLut;
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

    void ComputeIrradiance();

    static glm::mat4 s_Views[6];
    static glm::mat4 s_Projection;
    static VkBuffer s_VertexBuffer;
    static VmaAllocation s_VertexBufferAllocation;

    static VmaAllocator s_MemoryAllocator;

    static VkRenderPass s_RenderPass;
    static VkDescriptorSetLayout s_DescriptorSetLayout;
    static VkPipelineLayout s_PipelineLayout;
    static VkPipeline s_Pipeline;

    static VkRenderPass s_IrradianceRenderPass;
    static VkDescriptorSetLayout s_IrradianceDescriptorSetLayout;
    static VkPipelineLayout s_IrradiancePipelineLayout;
    static VkPipeline s_IrradiancePipeline;

    static VkDevice s_Device;
    static VulkanCommandBuffer* s_CommandBuffer;
    static VkDescriptorPool s_DescriptorPool;

    static void InitRenderPass();
    static void InitPipeline();
    static void InitIrradianceRenderPass();
    static void InitIrradiancePipeline();
    static void TransitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, uint32_t layerCount);
    static void TransitionImageEx(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                           uint32_t baseMip, uint32_t mipCount, uint32_t baseLayer, uint32_t layerCount);
    static void CreateVertexBuffer(VulkanCommandBuffer commandBuffer);
  };
}
