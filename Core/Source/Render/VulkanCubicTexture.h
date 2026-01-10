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

    static void DrawCube(VkCommandBuffer cmd);

  private:

    VulkanTexture m_EquirectTexture;

    VkImage m_CubemapImage {};
    VkImageView m_CubemapImageView {};
    VmaAllocation m_CubemapImageAllocation {};
    VkSampler m_CubeMapSampler {};
    VkImageView m_FaceViews[6] {};
    VkFramebuffer m_FrameBuffers[6] {};

    static VkDevice s_Device;
    static VmaAllocator s_MemoryAllocator;
    static VkRenderPass s_RenderPass;
    static glm::mat4 s_Views[6];
    static glm::mat4 s_Projection;
    static VkDescriptorSetLayout s_DescriptorSetLayout;
    static VkPipelineLayout s_PipelineLayout;
    static VkPipeline s_Pipeline;
    static VkBuffer s_VertexBuffer;
    static VmaAllocation s_VertexBufferAllocation;
    static VulkanCommandBuffer* s_CommandBuffer;
    static VkDescriptorPool s_DescriptorPool;

    static void InitRenderPass();
    static void InitPipeline();
    static void TransitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, uint32_t layerCount);
    static void TransitionImageEx(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                           uint32_t baseMip, uint32_t mipCount, uint32_t baseLayer, uint32_t layerCount);
    static void CreateVertexBuffer(VulkanCommandBuffer commandBuffer);
  };
}
