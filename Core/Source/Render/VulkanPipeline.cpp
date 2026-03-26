#include "VulkanPipeline.h"

#include "ShaderUtils.h"
#include "Log.h"

namespace YAEngine
{

  void VulkanPipeline::Init(VkDevice device, VkRenderPass renderPass, const PipelineCreateInfo& info, VkPipelineCache pipelineCache)
  {
    m_Device = device;
    m_PushConstantSize = info.pushConstantSize;

    auto vertShaderCode = ReadShaderFile(info.vertexShaderFile);
    VkShaderModule vertShaderModule = CreateShaderModule(m_Device, vertShaderCode);

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";
    shaderStages.push_back(vertShaderStageInfo);

    VkShaderModule fragShaderModule = VK_NULL_HANDLE;
    if (!info.fragmentShaderFile.empty())
    {
      auto fragShaderCode = ReadShaderFile(info.fragmentShaderFile);
      fragShaderModule = CreateShaderModule(m_Device, fragShaderCode);

      VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
      fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
      fragShaderStageInfo.module = fragShaderModule;
      fragShaderStageInfo.pName = "main";
      shaderStages.push_back(fragShaderStageInfo);
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    auto attributeDescriptions = GetVertexInputAttributeDescriptions(info.vertexInputFormat, &bindingDescription.stride);

    vertexInputInfo.vertexBindingDescriptionCount = !!attributeDescriptions.size();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = attributeDescriptions.size() ? &bindingDescription : nullptr;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = info.doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendTemplate{};
    blendTemplate.blendEnable = info.blending ? VK_TRUE : VK_FALSE;
    blendTemplate.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendTemplate.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendTemplate.colorBlendOp = VK_BLEND_OP_ADD;
    blendTemplate.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendTemplate.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendTemplate.alphaBlendOp = VK_BLEND_OP_ADD;
    blendTemplate.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;

    std::vector<VkPipelineColorBlendAttachmentState> attachments(info.colorAttachmentCount, blendTemplate);

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = info.colorAttachmentCount;
    colorBlending.pAttachments = attachments.data();
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    std::vector<VkDynamicState> dynamicStates = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_ALL;
    range.offset = 0;
    range.size = m_PushConstantSize;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &range;
    pipelineLayoutInfo.pSetLayouts = info.sets.data();
    pipelineLayoutInfo.setLayoutCount = (uint32_t)info.sets.size();

    if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create pipeline layout");
      throw std::runtime_error("failed to create pipeline layout!");
    }

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = info.depthTesting ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = info.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = info.compareOp;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_PipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(m_Device, pipelineCache, 1, &pipelineInfo, nullptr, &m_Pipeline) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create graphics pipeline");
      throw std::runtime_error("failed to create graphics pipeline!");
    }

    if (fragShaderModule != VK_NULL_HANDLE)
      vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
  }

  void VulkanPipeline::Destroy()
  {
    vkDestroyPipeline(m_Device, m_Pipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
  }

  void VulkanPipeline::Bind(VkCommandBuffer commandBuffer)
  {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
  }

  void VulkanPipeline::BindDescriptorSets(VkCommandBuffer commandBuffer, const std::vector<VkDescriptorSet>& descriptorSets, uint32_t set)
  {
    vkCmdBindDescriptorSets(
      commandBuffer,
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      m_PipelineLayout,
      set,
      (uint32_t)descriptorSets.size(),
      descriptorSets.data(),
      0,
      nullptr);
  }

  void VulkanPipeline::PushConstants(VkCommandBuffer cmd, void* data)
  {
    vkCmdPushConstants(
      cmd,
      m_PipelineLayout,
      VK_SHADER_STAGE_ALL,
      0,
      m_PushConstantSize,
      data
    );
  }

  std::vector<VkVertexInputAttributeDescription> VulkanPipeline::GetVertexInputAttributeDescriptions(
    std::string_view vertexInput, uint32_t* vertexSize)
  {
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};
    uint32_t location = 0;
    uint32_t offset = 0;

    *vertexSize = 0;
    auto parseComponent = [](char type, char count) -> VkFormat {
      switch (type)
      {
      case 'f':
        switch (count)
        {
        case '1': return VK_FORMAT_R32_SFLOAT;
        case '2': return VK_FORMAT_R32G32_SFLOAT;
        case '3': return VK_FORMAT_R32G32B32_SFLOAT;
        case '4': return VK_FORMAT_R32G32B32A32_SFLOAT;
        default:
          YA_LOG_ERROR("Render", "Failed to parse vertex input: invalid float component count '%c'", count);
          throw std::runtime_error("failed to parse vertex input!");
        }
        break;
      case 'u':
        switch (count)
        {
        case '1': return VK_FORMAT_R32_UINT;
        case '2': return VK_FORMAT_R32G32_UINT;
        case '3': return VK_FORMAT_R32G32B32_UINT;
        case '4': return VK_FORMAT_R32G32B32A32_UINT;
        default:
          YA_LOG_ERROR("Render", "Failed to parse vertex input: invalid uint component count '%c'", count);
          throw std::runtime_error("failed to parse vertex input!");
        }
          break;
      case 'i':
        switch (count)
        {
        case '1': return VK_FORMAT_R32_SINT;
        case '2': return VK_FORMAT_R32G32_SINT;
        case '3': return VK_FORMAT_R32G32B32_SINT;
        case '4': return VK_FORMAT_R32G32B32A32_SINT;
        default:
          YA_LOG_ERROR("Render", "Failed to parse vertex input: invalid int component count '%c'", count);
          throw std::runtime_error("failed to parse vertex input!");
        }
        break;
      }
      YA_LOG_ERROR("Render", "Invalid vertex input type '%c'", type);
      throw std::runtime_error("invalid vertex input type");
    };

    for (size_t i = 0; i + 1 < vertexInput.size(); i += 2) {
      char type = vertexInput[i];
      char count = vertexInput[i + 1];


      *vertexSize += static_cast<uint32_t>(count - '0') * sizeof(float);

      VkVertexInputAttributeDescription attr{};
      attr.binding = 0;
      attr.location = location++;
      attr.format = parseComponent(type, count);
      attr.offset = offset;

      switch (attr.format) {
      case VK_FORMAT_R32_SFLOAT:
      case VK_FORMAT_R32_UINT:
      case VK_FORMAT_R32_SINT:
          offset += 4; break;
      case VK_FORMAT_R32G32_SFLOAT:
      case VK_FORMAT_R32G32_UINT:
      case VK_FORMAT_R32G32_SINT:
          offset += 8; break;
      case VK_FORMAT_R32G32B32_SFLOAT:
      case VK_FORMAT_R32G32B32_UINT:
      case VK_FORMAT_R32G32B32_SINT:
          offset += 12; break;
      case VK_FORMAT_R32G32B32A32_SFLOAT:
      case VK_FORMAT_R32G32B32A32_UINT:
      case VK_FORMAT_R32G32B32A32_SINT:
          offset += 16; break;
      default:
          YA_LOG_ERROR("Render", "Unsupported vertex attribute format");
          throw std::runtime_error("unsupported format");
      }

      attributeDescriptions.push_back(attr);
    }

    return attributeDescriptions;
  }
}
