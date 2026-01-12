#include "VulkanCubicTexture.h"

#include <complex>

#include "VulkanCommandBuffer.h"
#include "VulkanMaterial.h"
#include "VulkanPipeline.h"

#include "StbImage/stb_image.h"

namespace YAEngine
{
  glm::mat4 VulkanCubicTexture::s_Views[] =
   {
    glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f, 0.0f, 0.0f), glm::vec3(0.0f,-1.0f, 0.0f)), // +X
    glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f,-1.0f, 0.0f)), // -X
    glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,-1.0f, 0.0f), glm::vec3(0.0f, 0.0f,-1.0f)), // -Y
    glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)), // +Y
    glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, 0.0f, 1.0f), glm::vec3(0.0f,-1.0f, 0.0f)), // +Z
    glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, 0.0f,-1.0f), glm::vec3(0.0f,-1.0f, 0.0f))  // -Z
  };
  glm::mat4 VulkanCubicTexture::s_Projection = glm::perspective(
    glm::radians(90.0f),
    1.0f,
    0.1f,
    9.0f
  );
  const glm::vec3 cubeVertices[36] = {
    // +X
    {1, -1, -1}, {1, -1, 1}, {1, 1, 1},
    {1, -1, -1}, {1, 1, 1}, {1, 1, -1},
    // -X
    {-1, -1, 1}, {-1, -1, -1}, {-1, 1, -1},
    {-1, -1, 1}, {-1, 1, -1}, {-1, 1, 1},
    // +Y
    {-1, 1, -1}, {1, 1, -1}, {1, 1, 1},
    {-1, 1, -1}, {1, 1, 1}, {-1, 1, 1},
    // -Y
    {-1, -1, 1}, {1, -1, 1}, {1, -1, -1},
    {-1, -1, 1}, {1, -1, -1}, {-1, -1, -1},
    // +Z
    {-1, -1, 1}, {-1, 1, 1}, {1, 1, 1},
    {-1, -1, 1}, {1, 1, 1}, {1, -1, 1},
    // -Z
    {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
    {1, -1, -1}, {-1, 1, -1}, {-1, -1, -1},
  };
  VkDevice VulkanCubicTexture::s_Device {};
  VmaAllocator VulkanCubicTexture::s_MemoryAllocator {};
  VkDescriptorPool VulkanCubicTexture::s_DescriptorPool {};
  VulkanCommandBuffer* VulkanCubicTexture::s_CommandBuffer {};

  VkRenderPass VulkanCubicTexture::s_RenderPass {};
  VkDescriptorSetLayout VulkanCubicTexture::s_DescriptorSetLayout {};
  VkPipelineLayout VulkanCubicTexture::s_PipelineLayout {};
  VkPipeline VulkanCubicTexture::s_Pipeline {};

  VkRenderPass VulkanCubicTexture::s_IrradianceRenderPass {};
  VkDescriptorSetLayout VulkanCubicTexture::s_IrradianceDescriptorSetLayout {};
  VkPipelineLayout VulkanCubicTexture::s_IrradiancePipelineLayout {};
  VkPipeline VulkanCubicTexture::s_IrradiancePipeline {};

  VkBuffer VulkanCubicTexture::s_VertexBuffer {};
  VmaAllocation VulkanCubicTexture::s_VertexBufferAllocation {};

  VulkanTexture VulkanCubicTexture::m_BRDFLut {};

  void VulkanCubicTexture::Create(void* data, uint32_t width, uint32_t height)
  {
    m_EquirectTexture.Load(data, width, height, sizeof(float) * 4, VK_FORMAT_R32G32B32A32_SFLOAT);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { 1024, 1024, 1 };
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.mipLevels = 10;
    imageInfo.arrayLayers = 6;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    vmaCreateImage(s_MemoryAllocator, &imageInfo, &allocInfo, &m_CubemapImage, &m_CubemapImageAllocation, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_CubemapImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;

    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 10;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    vkCreateImageView(s_Device, &viewInfo, nullptr, &m_CubemapImageView);

    for (uint32_t face = 0; face < 6; face++)
    {
      VkImageViewCreateInfo view{};
      view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      view.image = m_CubemapImage;
      view.viewType = VK_IMAGE_VIEW_TYPE_2D;
      view.format = VK_FORMAT_R16G16B16A16_SFLOAT;

      view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      view.subresourceRange.baseMipLevel = 0;
      view.subresourceRange.levelCount = 1;
      view.subresourceRange.baseArrayLayer = face;
      view.subresourceRange.layerCount = 1;

      vkCreateImageView(s_Device, &view, nullptr, &m_FaceViews[face]);
    }

    for (uint32_t face = 0; face < 6; face++)
    {
      VkFramebufferCreateInfo fb{};
      fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fb.renderPass = s_RenderPass;
      fb.attachmentCount = 1;
      fb.pAttachments = &m_FaceViews[face];
      fb.width  = 1024;
      fb.height = 1024;
      fb.layers = 1;

      vkCreateFramebuffer(s_Device, &fb, nullptr, &m_FrameBuffers[face]);
    }

    auto cmd = s_CommandBuffer->BeginSingleTimeCommands();
    TransitionImage(
      cmd,
      m_CubemapImage,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      10,
      6
    );
    s_CommandBuffer->EndSingleTimeCommands(cmd);

    VkDescriptorImageInfo descriptorImageInfo{};
    descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descriptorImageInfo.imageView   = m_EquirectTexture.GetView();
    descriptorImageInfo.sampler     = m_EquirectTexture.GetSampler();

    VkDescriptorSetLayout layouts[] = { s_DescriptorSetLayout };

    VkDescriptorSetAllocateInfo descriptorAllocInfo{};
    descriptorAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorAllocInfo.descriptorPool = s_DescriptorPool;
    descriptorAllocInfo.descriptorSetCount = 1;
    descriptorAllocInfo.pSetLayouts = layouts;

    VkDescriptorSet descriptorSet;
    vkAllocateDescriptorSets(s_Device, &descriptorAllocInfo, &descriptorSet);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &descriptorImageInfo;

    vkUpdateDescriptorSets(s_Device, 1, &write, 0, nullptr);

    for (uint32_t face = 0; face < 6; face++)
    {
      VkRenderPassBeginInfo rp{};
      rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      rp.renderPass = s_RenderPass;
      rp.framebuffer = m_FrameBuffers[face];
      rp.renderArea.extent = { 1024, 1024 };

      VkClearValue clear{};
      clear.color = { 0, 0, 0, 1 };
      rp.clearValueCount = 1;
      rp.pClearValues = &clear;

      auto cmd = s_CommandBuffer->BeginSingleTimeCommands();
      vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_Pipeline);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              s_PipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

      VkViewport vp{ 0, 0, (float)1024, (float)1024, 0, 1 };
      VkRect2D scissor{ {0,0}, {1024, 1024} };
      vkCmdSetViewport(cmd, 0, 1, &vp);
      vkCmdSetScissor(cmd, 0, 1, &scissor);

      glm::mat4 vpMat = s_Projection * s_Views[face];
      vkCmdPushConstants(cmd, s_PipelineLayout,
                         VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &vpMat);

      DrawCube(cmd);

      vkCmdEndRenderPass(cmd);

      s_CommandBuffer->EndSingleTimeCommands(cmd);
    }

    auto cmdBuffer = s_CommandBuffer->BeginSingleTimeCommands();

    uint32_t mipW = 1024;
    uint32_t mipH = 1024;

    TransitionImageEx(cmdBuffer, m_CubemapImage,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      0, 1, 0, 6);

    for (uint32_t i = 1; i < 10; i++)
    {
      TransitionImageEx(
        cmdBuffer,
        m_CubemapImage,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        i,
        1,
        0,
        6
      );

      VkImageBlit blit{};
      blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      blit.srcSubresource.mipLevel = i - 1;
      blit.srcSubresource.baseArrayLayer = 0;
      blit.srcSubresource.layerCount = 6;

      blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      blit.dstSubresource.mipLevel = i;
      blit.dstSubresource.baseArrayLayer = 0;
      blit.dstSubresource.layerCount = 6;

      blit.srcOffsets[0] = { 0, 0, 0 };
      blit.srcOffsets[1] = { int32_t(mipW), int32_t(mipH), 1 };

      mipW = std::max(1u, mipW >> 1);
      mipH = std::max(1u, mipH >> 1);

      blit.dstOffsets[0] = { 0, 0, 0 };
      blit.dstOffsets[1] = { int32_t(mipW), int32_t(mipH), 1 };

      vkCmdBlitImage(
        cmdBuffer,
        m_CubemapImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        m_CubemapImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &blit,
        VK_FILTER_LINEAR
      );

      TransitionImageEx(
        cmdBuffer,
        m_CubemapImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        i,
        1,
        0,
        6
      );
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_CubemapImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 10;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 6;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;

    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;

    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 10.0f;

    if (vkCreateSampler(s_Device, &samplerInfo, nullptr, &m_CubeMapSampler) != VK_SUCCESS)
    {
      throw std::runtime_error("Failed to create texture sampler!");
    }

    s_CommandBuffer->EndSingleTimeCommands(cmdBuffer);

    ComputeIrradiance();
  }

  void VulkanCubicTexture::Destroy()
  {
    m_EquirectTexture.Destroy();

    vkDestroyImageView(s_Device, m_CubemapImageView, nullptr);
    vmaDestroyImage(s_MemoryAllocator, m_CubemapImage, m_CubemapImageAllocation);
    vkDestroySampler(s_Device, m_CubeMapSampler, nullptr);

    vkDestroyImageView(s_Device, m_IrradianceImageView, nullptr);
    vmaDestroyImage(s_MemoryAllocator, m_IrradianceImage, m_IrradianceImageAllocation);
    vkDestroySampler(s_Device, m_IrradianceSampler, nullptr);

    for (uint32_t face = 0; face < 6; face++)
    {
      vkDestroyFramebuffer(s_Device, m_FrameBuffers[face], nullptr);
      vkDestroyImageView(s_Device, m_FaceViews[face], nullptr);
      vkDestroyFramebuffer(s_Device, m_IrradianceFrameBuffers[face], nullptr);
      vkDestroyImageView(s_Device, m_IrradianceFaceViews[face], nullptr);
    }
  }

  void VulkanCubicTexture::TransitionImage(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32_t mipLevels,
    uint32_t layerCount)
  {
    TransitionImageEx(
        cmd,
        image,
        oldLayout,
        newLayout,
        0,
        mipLevels,
        0,
        layerCount
    );
  }

  void VulkanCubicTexture::TransitionImageEx(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32_t baseMip,
    uint32_t mipCount,
    uint32_t baseLayer,
    uint32_t layerCount)
  {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;

    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = baseMip;
    barrier.subresourceRange.levelCount = mipCount;
    barrier.subresourceRange.baseArrayLayer = baseLayer;
    barrier.subresourceRange.layerCount = layerCount;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = 0;

    vkCmdPipelineBarrier(
        cmd,
        srcStage, dstStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
  }

  void VulkanCubicTexture::CreateVertexBuffer(VulkanCommandBuffer commandBuffer)
  {
    VkDeviceSize bufferSize = 36 * sizeof(glm::vec3);

    VkBufferCreateInfo stagingBufferInfo{};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = bufferSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocation stagingAlloc;
    VkBuffer stagingBuffer;
    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    if (vmaCreateBuffer(s_MemoryAllocator, &stagingBufferInfo, &stagingAllocInfo, &stagingBuffer, &stagingAlloc, nullptr) != VK_SUCCESS)
    {
      throw std::exception("Failed to create vertex staging buffer");
    }

    void* data;
    vmaMapMemory(s_MemoryAllocator, stagingAlloc, &data);
    memcpy(data, cubeVertices, (size_t)bufferSize);
    vmaUnmapMemory(s_MemoryAllocator, stagingAlloc);

    VkBufferCreateInfo vertexBufferInfo{};
    vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vertexBufferInfo.size = bufferSize;
    vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo vertexAllocInfo{};
    vertexAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(s_MemoryAllocator, &vertexBufferInfo, &vertexAllocInfo, &s_VertexBuffer, &s_VertexBufferAllocation, nullptr) != VK_SUCCESS)
    {
      throw std::exception("Failed to create vertex buffer");
    }

    VkCommandBuffer cmd = commandBuffer.BeginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;

    vkCmdCopyBuffer(cmd, stagingBuffer, s_VertexBuffer, 1, &copyRegion);

    commandBuffer.EndSingleTimeCommands(cmd);
    vmaDestroyBuffer(s_MemoryAllocator, stagingBuffer, stagingAlloc);
  }

  void VulkanCubicTexture::InitCubicTextures(VkDevice device, VmaAllocator allocator, VulkanCommandBuffer& commandBuffer, VkDescriptorPool descriptorPool)
  {
    s_Device = device;
    s_Projection[1][1] *= -1;
    s_MemoryAllocator = allocator;
    s_CommandBuffer = &commandBuffer;
    s_DescriptorPool = descriptorPool;

    InitRenderPass();
    InitPipeline();
    InitIrradianceRenderPass();
    InitIrradiancePipeline();
    CreateVertexBuffer(commandBuffer);

    int w, h, channels;
    float* src = stbi_loadf(
        WORKING_DIR "/Assets/Textures/BRDFLut.png",
        &w, &h, &channels,
        4 // RGBA
    );

    if (src)
    {
      std::vector<float> rgData;
      rgData.resize(w * h * 2);

      for (int i = 0; i < w * h; ++i)
      {
        rgData[i * 2 + 0] = src[i * 4 + 0];
        rgData[i * 2 + 1] = src[i * 4 + 1];
      }

      stbi_image_free(src);

      m_BRDFLut.Load(
          rgData.data(),
          w,
          h,
          sizeof(float) * 2,
          VK_FORMAT_R32G32_SFLOAT,
          false
      );
    }
    else
    {
      throw std::exception("Failed to load BRDFLut.png");
    }
  }

  void VulkanCubicTexture::DestroyCubicTextures()
  {
    vkDestroyPipeline(s_Device, s_Pipeline, nullptr);
    vkDestroyPipelineLayout(s_Device, s_PipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(s_Device, s_DescriptorSetLayout, nullptr);
    vkDestroyRenderPass(s_Device, s_RenderPass, nullptr);

    vkDestroyPipeline(s_Device, s_IrradiancePipeline, nullptr);
    vkDestroyPipelineLayout(s_Device, s_IrradiancePipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(s_Device, s_IrradianceDescriptorSetLayout, nullptr);
    vkDestroyRenderPass(s_Device, s_IrradianceRenderPass, nullptr);

    vmaDestroyBuffer(s_MemoryAllocator, s_VertexBuffer, s_VertexBufferAllocation);
    m_BRDFLut.Destroy();
  }

  void VulkanCubicTexture::DrawCube(VkCommandBuffer cmd)
  {
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &s_VertexBuffer, &offset);
    vkCmdDraw(cmd, 36, 1, 0, 0);
  }

  void VulkanCubicTexture::InitRenderPass()
  {
    VkAttachmentDescription color{};
    color.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &color;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;

    if (vkCreateRenderPass(s_Device, &info, nullptr, &s_RenderPass) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create render pass!");
    }
  }

  void VulkanCubicTexture::InitPipeline()
  {
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setInfo.bindingCount = 1;
    setInfo.pBindings = &samplerBinding;

    if (vkCreateDescriptorSetLayout(s_Device, &setInfo, nullptr, &s_DescriptorSetLayout) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create descriptor set layout!");
    }

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.offset = 0;
    push.size = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &s_DescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;

    if (vkCreatePipelineLayout(s_Device, &layoutInfo, nullptr, &s_PipelineLayout) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create pipeline layout!");
    }

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = VulkanPipeline::CreateShaderModule(s_Device, VulkanPipeline::ReadFile("cubemap.vert"));
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = VulkanPipeline::CreateShaderModule(s_Device, VulkanPipeline::ReadFile("cubemap.frag"));
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = { vertStage, fragStage };

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(glm::vec3);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr{};
    attr.binding = 0;
    attr.location = 0;
    attr.format = VK_FORMAT_R32G32B32_SFLOAT;
    attr.offset = 0;

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attr;

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkDynamicState dynamics[] =
    {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamics;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msaa{};
    msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blending{};
    blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &assembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &msaa;
    pipelineInfo.pColorBlendState = &blending;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = s_PipelineLayout;
    pipelineInfo.renderPass = s_RenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(s_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &s_Pipeline) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create graphics pipeline");
    }

    vkDestroyShaderModule(s_Device, vertStage.module, nullptr);
    vkDestroyShaderModule(s_Device, fragStage.module, nullptr);
  }

  void VulkanCubicTexture::ComputeIrradiance()
  {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { 32, 32, 1 };
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 6;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    vmaCreateImage(s_MemoryAllocator, &imageInfo, &allocInfo, &m_IrradianceImage, &m_IrradianceImageAllocation, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_IrradianceImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;

    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    vkCreateImageView(s_Device, &viewInfo, nullptr, &m_IrradianceImageView);

    for (uint32_t face = 0; face < 6; face++)
    {
      VkImageViewCreateInfo view{};
      view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      view.image = m_IrradianceImage;
      view.viewType = VK_IMAGE_VIEW_TYPE_2D;
      view.format = VK_FORMAT_R16G16B16A16_SFLOAT;

      view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      view.subresourceRange.baseMipLevel = 0;
      view.subresourceRange.levelCount = 1;
      view.subresourceRange.baseArrayLayer = face;
      view.subresourceRange.layerCount = 1;

      vkCreateImageView(s_Device, &view, nullptr, &m_IrradianceFaceViews[face]);
    }

    for (uint32_t face = 0; face < 6; face++)
    {
      VkFramebufferCreateInfo fb{};
      fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fb.renderPass = s_RenderPass;
      fb.attachmentCount = 1;
      fb.pAttachments = &m_IrradianceFaceViews[face];
      fb.width  = 32;
      fb.height = 32;
      fb.layers = 1;

      vkCreateFramebuffer(s_Device, &fb, nullptr, &m_IrradianceFrameBuffers[face]);
    }

    auto cmd = s_CommandBuffer->BeginSingleTimeCommands();
    TransitionImage(
      cmd,
      m_IrradianceImage,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      1,
      6
    );
    s_CommandBuffer->EndSingleTimeCommands(cmd);

    VkDescriptorImageInfo descriptorImageInfo{};
    descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descriptorImageInfo.imageView   = GetView();
    descriptorImageInfo.sampler     = GetSampler();

    VkDescriptorSetLayout layouts[] = { s_IrradianceDescriptorSetLayout };

    VkDescriptorSetAllocateInfo descriptorAllocInfo{};
    descriptorAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorAllocInfo.descriptorPool = s_DescriptorPool;
    descriptorAllocInfo.descriptorSetCount = 1;
    descriptorAllocInfo.pSetLayouts = layouts;

    VkDescriptorSet descriptorSet;
    vkAllocateDescriptorSets(s_Device, &descriptorAllocInfo, &descriptorSet);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &descriptorImageInfo;

    vkUpdateDescriptorSets(s_Device, 1, &write, 0, nullptr);

    for (uint32_t face = 0; face < 6; face++)
    {
      VkRenderPassBeginInfo rp{};
      rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      rp.renderPass = s_IrradianceRenderPass;
      rp.framebuffer = m_IrradianceFrameBuffers[face];
      rp.renderArea.extent = { 32, 32 };

      VkClearValue clear{};
      clear.color = { 0, 0, 0, 1 };
      rp.clearValueCount = 1;
      rp.pClearValues = &clear;

      cmd = s_CommandBuffer->BeginSingleTimeCommands();
      vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_IrradiancePipeline);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              s_IrradiancePipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

      VkViewport vp{ 0, 0, (float)32, (float)32, 0, 1 };
      VkRect2D scissor{ {0,0}, {32, 32} };
      vkCmdSetViewport(cmd, 0, 1, &vp);
      vkCmdSetScissor(cmd, 0, 1, &scissor);

      glm::mat4 vpMat = s_Projection * s_Views[face];
      vkCmdPushConstants(cmd, s_IrradiancePipelineLayout,
                         VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &vpMat);

      DrawCube(cmd);

      vkCmdEndRenderPass(cmd);

      s_CommandBuffer->EndSingleTimeCommands(cmd);
    }

    cmd = s_CommandBuffer->BeginSingleTimeCommands();
    TransitionImage(
      cmd,
      m_IrradianceImage,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      1,
      6);
    s_CommandBuffer->EndSingleTimeCommands(cmd);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;

    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;

    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;

    if (vkCreateSampler(s_Device, &samplerInfo, nullptr, &m_IrradianceSampler) != VK_SUCCESS)
    {
      throw std::runtime_error("Failed to create texture sampler!");
    }
  }

  void VulkanCubicTexture::InitIrradianceRenderPass()
  {
    VkAttachmentDescription color{};
    color.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &color;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;

    if (vkCreateRenderPass(s_Device, &info, nullptr, &s_IrradianceRenderPass) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create render pass!");
    }
  }

  void VulkanCubicTexture::InitIrradiancePipeline()
  {
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setInfo.bindingCount = 1;
    setInfo.pBindings = &samplerBinding;

    if (vkCreateDescriptorSetLayout(s_Device, &setInfo, nullptr, &s_IrradianceDescriptorSetLayout) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create descriptor set layout!");
    }

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.offset = 0;
    push.size = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &s_IrradianceDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;

    if (vkCreatePipelineLayout(s_Device, &layoutInfo, nullptr, &s_IrradiancePipelineLayout) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create pipeline layout!");
    }

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = VulkanPipeline::CreateShaderModule(s_Device, VulkanPipeline::ReadFile("cubemap.vert"));
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = VulkanPipeline::CreateShaderModule(s_Device, VulkanPipeline::ReadFile("irradiance.frag"));
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = { vertStage, fragStage };

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(glm::vec3);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr{};
    attr.binding = 0;
    attr.location = 0;
    attr.format = VK_FORMAT_R32G32B32_SFLOAT;
    attr.offset = 0;

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attr;

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkDynamicState dynamics[] =
    {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamics;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msaa{};
    msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blending{};
    blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &assembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &msaa;
    pipelineInfo.pColorBlendState = &blending;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = s_IrradiancePipelineLayout;
    pipelineInfo.renderPass = s_IrradianceRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(s_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &s_IrradiancePipeline) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create graphics pipeline");
    }

    vkDestroyShaderModule(s_Device, vertStage.module, nullptr);
    vkDestroyShaderModule(s_Device, fragStage.module, nullptr);
  }

}
