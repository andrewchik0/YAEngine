#include "VulkanCubicTexture.h"

#include "RenderContext.h"
#include "VulkanCommandBuffer.h"
#include "VulkanDescriptorPool.h"
#include "Utils/Log.h"
#include "ShaderUtils.h"
#include "ImageBarrier.h"

#include <Stb/stb_image.h>

namespace YAEngine
{
  static const glm::vec3 cubeVertices[36] = {
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

  void CubicTextureResources::Init(const RenderContext& ctx)
  {
    views[0] = glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f, 0.0f, 0.0f), glm::vec3(0.0f,-1.0f, 0.0f));
    views[1] = glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f,-1.0f, 0.0f));
    views[2] = glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,-1.0f, 0.0f), glm::vec3(0.0f, 0.0f,-1.0f));
    views[3] = glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    views[4] = glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, 0.0f, 1.0f), glm::vec3(0.0f,-1.0f, 0.0f));
    views[5] = glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, 0.0f,-1.0f), glm::vec3(0.0f,-1.0f, 0.0f));

    projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 9.0f);
    projection[1][1] *= -1;

    InitRenderPass(ctx.device, renderPass, "cubemap");
    InitPipeline(ctx.device, renderPass, "cubemap.frag",
                 sizeof(glm::mat4), VK_SHADER_STAGE_VERTEX_BIT,
                 descriptorSetLayout, pipelineLayout, pipeline, "cubemap");

    InitRenderPass(ctx.device, irradianceRenderPass, "irradiance");
    InitPipeline(ctx.device, irradianceRenderPass, "irradiance.frag",
                 sizeof(glm::mat4), VK_SHADER_STAGE_VERTEX_BIT,
                 irradianceDescriptorSetLayout, irradiancePipelineLayout, irradiancePipeline, "irradiance");

    InitRenderPass(ctx.device, prefilterRenderPass, "prefilter");
    InitPipeline(ctx.device, prefilterRenderPass, "prefilter.frag",
                 sizeof(glm::mat4) + sizeof(float), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                 prefilterDescriptorSetLayout, prefilterPipelineLayout, prefilterPipeline, "prefilter");
    CreateVertexBuffer(ctx);

    int w, h, channels;
    float* src = stbi_loadf(
        WORKING_DIR "/Assets/Textures/BRDFLut.png",
        &w, &h, &channels, 4);

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

      brdfLut.Load(ctx, rgData.data(), w, h, sizeof(float) * 2, VK_FORMAT_R32G32_SFLOAT, false);
    }
    else
    {
      YA_LOG_ERROR("Render", "Failed to load BRDFLut.png");
      throw std::runtime_error("Failed to load BRDFLut.png");
    }
  }

  void CubicTextureResources::Destroy(const RenderContext& ctx)
  {
    vkDestroyPipeline(ctx.device, pipeline, nullptr);
    vkDestroyPipelineLayout(ctx.device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, descriptorSetLayout, nullptr);
    vkDestroyRenderPass(ctx.device, renderPass, nullptr);

    vkDestroyPipeline(ctx.device, irradiancePipeline, nullptr);
    vkDestroyPipelineLayout(ctx.device, irradiancePipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, irradianceDescriptorSetLayout, nullptr);
    vkDestroyRenderPass(ctx.device, irradianceRenderPass, nullptr);

    vkDestroyPipeline(ctx.device, prefilterPipeline, nullptr);
    vkDestroyPipelineLayout(ctx.device, prefilterPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, prefilterDescriptorSetLayout, nullptr);
    vkDestroyRenderPass(ctx.device, prefilterRenderPass, nullptr);

    vmaDestroyBuffer(ctx.allocator, vertexBuffer, vertexBufferAllocation);
    brdfLut.Destroy(ctx);
  }

  void CubicTextureResources::CreateVertexBuffer(const RenderContext& ctx)
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

    if (vmaCreateBuffer(ctx.allocator, &stagingBufferInfo, &stagingAllocInfo, &stagingBuffer, &stagingAlloc, nullptr) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create vertex staging buffer");
      throw std::runtime_error("Failed to create vertex staging buffer");
    }

    void* data;
    vmaMapMemory(ctx.allocator, stagingAlloc, &data);
    memcpy(data, cubeVertices, (size_t)bufferSize);
    vmaUnmapMemory(ctx.allocator, stagingAlloc);

    VkBufferCreateInfo vertexBufferInfo{};
    vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vertexBufferInfo.size = bufferSize;
    vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo vertexAllocInfo{};
    vertexAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(ctx.allocator, &vertexBufferInfo, &vertexAllocInfo, &vertexBuffer, &vertexBufferAllocation, nullptr) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create vertex buffer");
      throw std::runtime_error("Failed to create vertex buffer");
    }

    VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(cmd, stagingBuffer, vertexBuffer, 1, &copyRegion);

    ctx.commandBuffer->EndSingleTimeCommands(cmd);
    vmaDestroyBuffer(ctx.allocator, stagingBuffer, stagingAlloc);
  }

  void CubicTextureResources::InitRenderPass(VkDevice device, VkRenderPass& outRenderPass, const char* debugName)
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

    if (vkCreateRenderPass(device, &info, nullptr, &outRenderPass) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create %s render pass", debugName);
      throw std::runtime_error("failed to create render pass!");
    }
  }

  void CubicTextureResources::InitPipeline(VkDevice device, VkRenderPass rp, const char* fragShader,
                                            uint32_t pushConstantSize, VkShaderStageFlags pushStages,
                                            VkDescriptorSetLayout& outSetLayout, VkPipelineLayout& outPipelineLayout,
                                            VkPipeline& outPipeline, const char* debugName)
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

    if (vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &outSetLayout) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create %s descriptor set layout", debugName);
      throw std::runtime_error("failed to create descriptor set layout!");
    }

    VkPushConstantRange push{};
    push.stageFlags = pushStages;
    push.offset = 0;
    push.size = pushConstantSize;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &outSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &outPipelineLayout) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create %s pipeline layout", debugName);
      throw std::runtime_error("failed to create pipeline layout!");
    }

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = CreateShaderModule(device, ReadShaderFile("cubemap.vert"));
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = CreateShaderModule(device, ReadShaderFile(fragShader));
    fragStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = { vertStage, fragStageInfo };

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

    VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

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
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

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
    pipelineInfo.layout = outPipelineLayout;
    pipelineInfo.renderPass = rp;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &outPipeline) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create %s graphics pipeline", debugName);
      throw std::runtime_error("failed to create graphics pipeline");
    }

    vkDestroyShaderModule(device, vertStage.module, nullptr);
    vkDestroyShaderModule(device, fragStageInfo.module, nullptr);
  }

  void VulkanCubicTexture::Create(const RenderContext& ctx, CubicTextureResources& res, void* data, uint32_t width, uint32_t height)
  {
    m_EquirectTexture.Load(ctx, data, width, height, sizeof(float) * 4, VK_FORMAT_R32G32B32A32_SFLOAT);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { CUBEMAP_SIZE, CUBEMAP_SIZE, 1 };
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

    vmaCreateImage(ctx.allocator, &imageInfo, &allocInfo, &m_CubemapImage, &m_CubemapImageAllocation, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_CubemapImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    vkCreateImageView(ctx.device, &viewInfo, nullptr, &m_CubemapImageView);

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

      vkCreateImageView(ctx.device, &view, nullptr, &m_FaceViews[face]);
    }

    for (uint32_t face = 0; face < 6; face++)
    {
      VkFramebufferCreateInfo fb{};
      fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fb.renderPass = res.renderPass;
      fb.attachmentCount = 1;
      fb.pAttachments = &m_FaceViews[face];
      fb.width  = CUBEMAP_SIZE;
      fb.height = CUBEMAP_SIZE;
      fb.layers = 1;

      vkCreateFramebuffer(ctx.device, &fb, nullptr, &m_FrameBuffers[face]);
    }

    auto cmd = ctx.commandBuffer->BeginSingleTimeCommands();
    TransitionImageLayout(cmd, m_CubemapImage,
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);
    ctx.commandBuffer->EndSingleTimeCommands(cmd);

    VkDescriptorImageInfo descriptorImageInfo{};
    descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descriptorImageInfo.imageView   = m_EquirectTexture.GetView();
    descriptorImageInfo.sampler     = m_EquirectTexture.GetSampler();

    VkDescriptorSet descriptorSet = ctx.descriptorPool->Allocate(res.descriptorSetLayout);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &descriptorImageInfo;

    vkUpdateDescriptorSets(ctx.device, 1, &write, 0, nullptr);

    for (uint32_t face = 0; face < 6; face++)
    {
      VkRenderPassBeginInfo rp{};
      rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      rp.renderPass = res.renderPass;
      rp.framebuffer = m_FrameBuffers[face];
      rp.renderArea.extent = { CUBEMAP_SIZE, CUBEMAP_SIZE };

      VkClearValue clear{};
      clear.color = { 0, 0, 0, 1 };
      rp.clearValueCount = 1;
      rp.pClearValues = &clear;

      cmd = ctx.commandBuffer->BeginSingleTimeCommands();
      vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res.pipeline);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              res.pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

      VkViewport vp{ 0, 0, (float)CUBEMAP_SIZE, (float)CUBEMAP_SIZE, 0, 1 };
      VkRect2D scissor{ {0,0}, {CUBEMAP_SIZE, CUBEMAP_SIZE} };
      vkCmdSetViewport(cmd, 0, 1, &vp);
      vkCmdSetScissor(cmd, 0, 1, &scissor);

      glm::mat4 vpMat = res.projection * res.views[face];
      vkCmdPushConstants(cmd, res.pipelineLayout,
                         VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &vpMat);

      DrawCube(cmd, res);

      vkCmdEndRenderPass(cmd);
      ctx.commandBuffer->EndSingleTimeCommands(cmd);
    }

    cmd = ctx.commandBuffer->BeginSingleTimeCommands();
    TransitionImageLayout(cmd, m_CubemapImage,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);

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

    if (vkCreateSampler(ctx.device, &samplerInfo, nullptr, &m_CubeMapSampler) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create cubemap texture sampler");
      throw std::runtime_error("Failed to create texture sampler!");
    }

    ctx.commandBuffer->EndSingleTimeCommands(cmd);

    ComputeIrradiance(ctx, res);
    ComputePrefilter(ctx, res);
  }

  void VulkanCubicTexture::Destroy(const RenderContext& ctx)
  {
    m_EquirectTexture.Destroy(ctx);

    vkDestroyImageView(ctx.device, m_CubemapImageView, nullptr);
    vmaDestroyImage(ctx.allocator, m_CubemapImage, m_CubemapImageAllocation);
    vkDestroySampler(ctx.device, m_CubeMapSampler, nullptr);

    vkDestroyImageView(ctx.device, m_IrradianceImageView, nullptr);
    vmaDestroyImage(ctx.allocator, m_IrradianceImage, m_IrradianceImageAllocation);
    vkDestroySampler(ctx.device, m_IrradianceSampler, nullptr);

    vkDestroyImageView(ctx.device, m_PrefilterImageView, nullptr);
    vmaDestroyImage(ctx.allocator, m_PrefilterImage, m_PrefilterImageAllocation);
    vkDestroySampler(ctx.device, m_PrefilterSampler, nullptr);

    for (uint32_t face = 0; face < 6; face++)
    {
      vkDestroyFramebuffer(ctx.device, m_FrameBuffers[face], nullptr);
      vkDestroyImageView(ctx.device, m_FaceViews[face], nullptr);
      vkDestroyFramebuffer(ctx.device, m_IrradianceFrameBuffers[face], nullptr);
      vkDestroyImageView(ctx.device, m_IrradianceFaceViews[face], nullptr);

      for (uint32_t mip = 0; mip < CUBEMAP_MAX_MIP_LEVELS; mip++)
      {
        vkDestroyFramebuffer(ctx.device, m_PrefilterFrameBuffers[mip * 6 + face], nullptr);
        vkDestroyImageView(ctx.device, m_PrefilterFaceViews[mip * 6 + face], nullptr);
      }
    }
  }

  void VulkanCubicTexture::DrawCube(VkCommandBuffer cmd, const CubicTextureResources& res)
  {
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &res.vertexBuffer, &offset);
    vkCmdDraw(cmd, 36, 1, 0, 0);
  }

  void VulkanCubicTexture::ComputeIrradiance(const RenderContext& ctx, CubicTextureResources& res)
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

    vmaCreateImage(ctx.allocator, &imageInfo, &allocInfo, &m_IrradianceImage, &m_IrradianceImageAllocation, nullptr);

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

    vkCreateImageView(ctx.device, &viewInfo, nullptr, &m_IrradianceImageView);

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

      vkCreateImageView(ctx.device, &view, nullptr, &m_IrradianceFaceViews[face]);
    }

    for (uint32_t face = 0; face < 6; face++)
    {
      VkFramebufferCreateInfo fb{};
      fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fb.renderPass = res.renderPass;
      fb.attachmentCount = 1;
      fb.pAttachments = &m_IrradianceFaceViews[face];
      fb.width  = 32;
      fb.height = 32;
      fb.layers = 1;

      vkCreateFramebuffer(ctx.device, &fb, nullptr, &m_IrradianceFrameBuffers[face]);
    }

    auto cmd = ctx.commandBuffer->BeginSingleTimeCommands();
    TransitionImageLayout(cmd, m_IrradianceImage,
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);
    ctx.commandBuffer->EndSingleTimeCommands(cmd);

    VkDescriptorImageInfo descriptorImageInfo{};
    descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descriptorImageInfo.imageView   = GetView();
    descriptorImageInfo.sampler     = GetSampler();

    VkDescriptorSet descriptorSet = ctx.descriptorPool->Allocate(res.irradianceDescriptorSetLayout);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &descriptorImageInfo;

    vkUpdateDescriptorSets(ctx.device, 1, &write, 0, nullptr);

    for (uint32_t face = 0; face < 6; face++)
    {
      VkRenderPassBeginInfo rp{};
      rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      rp.renderPass = res.irradianceRenderPass;
      rp.framebuffer = m_IrradianceFrameBuffers[face];
      rp.renderArea.extent = { 32, 32 };

      VkClearValue clear{};
      clear.color = { 0, 0, 0, 1 };
      rp.clearValueCount = 1;
      rp.pClearValues = &clear;

      cmd = ctx.commandBuffer->BeginSingleTimeCommands();
      vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res.irradiancePipeline);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              res.irradiancePipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

      VkViewport vp{ 0, 0, 32.0f, 32.0f, 0, 1 };
      VkRect2D scissor{ {0,0}, {32, 32} };
      vkCmdSetViewport(cmd, 0, 1, &vp);
      vkCmdSetScissor(cmd, 0, 1, &scissor);

      glm::mat4 vpMat = res.projection * res.views[face];
      vkCmdPushConstants(cmd, res.irradiancePipelineLayout,
                         VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &vpMat);

      DrawCube(cmd, res);

      vkCmdEndRenderPass(cmd);
      ctx.commandBuffer->EndSingleTimeCommands(cmd);
    }

    cmd = ctx.commandBuffer->BeginSingleTimeCommands();
    TransitionImageLayout(cmd, m_IrradianceImage,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);
    ctx.commandBuffer->EndSingleTimeCommands(cmd);

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

    if (vkCreateSampler(ctx.device, &samplerInfo, nullptr, &m_IrradianceSampler) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create irradiance texture sampler");
      throw std::runtime_error("Failed to create texture sampler!");
    }
  }

  void VulkanCubicTexture::ComputePrefilter(const RenderContext& ctx, CubicTextureResources& res)
  {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { CUBEMAP_SIZE, CUBEMAP_SIZE, 1 };
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.mipLevels = CUBEMAP_MAX_MIP_LEVELS;
    imageInfo.arrayLayers = 6;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    vmaCreateImage(ctx.allocator, &imageInfo, &allocInfo, &m_PrefilterImage, &m_PrefilterImageAllocation, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_PrefilterImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = CUBEMAP_MAX_MIP_LEVELS;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    vkCreateImageView(ctx.device, &viewInfo, nullptr, &m_PrefilterImageView);

    for (uint32_t mip = 0; mip < CUBEMAP_MAX_MIP_LEVELS; mip++)
    {
      for (uint32_t face = 0; face < 6; face++)
      {
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = m_PrefilterImage;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.baseMipLevel = mip;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.baseArrayLayer = face;
        view.subresourceRange.layerCount = 1;

        vkCreateImageView(ctx.device, &view, nullptr, &m_PrefilterFaceViews[mip * 6 + face]);
      }
    }

    for (uint32_t mip = 0; mip < CUBEMAP_MAX_MIP_LEVELS; mip++)
    {
      uint32_t size = CUBEMAP_SIZE >> mip;
      for (uint32_t face = 0; face < 6; face++)
      {
        VkFramebufferCreateInfo fb{};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = res.prefilterRenderPass;
        fb.attachmentCount = 1;
        fb.pAttachments = &m_PrefilterFaceViews[mip * 6 + face];
        fb.width  = size;
        fb.height = size;
        fb.layers = 1;

        vkCreateFramebuffer(ctx.device, &fb, nullptr, &m_PrefilterFrameBuffers[mip * 6 + face]);
      }
    }

    auto cmd = ctx.commandBuffer->BeginSingleTimeCommands();
    TransitionImageLayout(cmd, m_PrefilterImage,
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, CUBEMAP_MAX_MIP_LEVELS, 0, 6);
    ctx.commandBuffer->EndSingleTimeCommands(cmd);

    VkDescriptorImageInfo descriptorImageInfo{};
    descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descriptorImageInfo.imageView   = GetView();
    descriptorImageInfo.sampler     = GetSampler();

    VkDescriptorSet descriptorSet = ctx.descriptorPool->Allocate(res.prefilterDescriptorSetLayout);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &descriptorImageInfo;

    vkUpdateDescriptorSets(ctx.device, 1, &write, 0, nullptr);

    for (uint32_t mip = 0; mip < CUBEMAP_MAX_MIP_LEVELS; mip++)
    {
      auto roughness = float(mip) / float(CUBEMAP_MAX_MIP_LEVELS - 1);
      uint32_t size = CUBEMAP_SIZE >> mip;

      for (uint32_t face = 0; face < 6; face++)
      {
        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = res.prefilterRenderPass;
        rp.framebuffer = m_PrefilterFrameBuffers[mip * 6 + face];
        rp.renderArea.extent = { size, size };

        VkClearValue clear{};
        clear.color = { 0, 0, 0, 1 };
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;

        cmd = ctx.commandBuffer->BeginSingleTimeCommands();
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res.prefilterPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                res.prefilterPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

        VkViewport vp{ 0, 0, (float)size, (float)size, 0, 1 };
        VkRect2D scissor{ {0,0}, {size, size} };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        struct
        {
          glm::mat4 mat;
          float roughness;
        } pushData {};

        pushData.mat = res.projection * res.views[face];
        pushData.roughness = roughness;

        vkCmdPushConstants(cmd, res.prefilterPipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(glm::mat4) + sizeof(float), &pushData);

        DrawCube(cmd, res);

        vkCmdEndRenderPass(cmd);
        ctx.commandBuffer->EndSingleTimeCommands(cmd);
      }
    }

    cmd = ctx.commandBuffer->BeginSingleTimeCommands();
    TransitionImageLayout(cmd, m_PrefilterImage,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, CUBEMAP_MAX_MIP_LEVELS, 0, 6);

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
    samplerInfo.maxLod = static_cast<float>(CUBEMAP_MAX_MIP_LEVELS);

    if (vkCreateSampler(ctx.device, &samplerInfo, nullptr, &m_PrefilterSampler) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create prefilter texture sampler");
      throw std::runtime_error("Failed to create texture sampler!");
    }

    ctx.commandBuffer->EndSingleTimeCommands(cmd);
  }
}
