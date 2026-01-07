#include "VulkanPipeline.h"

#include <fstream>
#include <ios>

namespace YAEngine
{

  void VulkanPipeline::Init(VkDevice device, VkRenderPass renderPass, const PipelineCreateInfo& info)
  {
    m_Device = device;

    auto vertShaderCode = ReadFile(info.vertexShaderFile);
    auto fragShaderCode = ReadFile(info.fragmentShaderFile);

    VkShaderModule vertShaderModule = CreateShaderModule(m_Device, vertShaderCode);
    VkShaderModule fragShaderModule = CreateShaderModule(m_Device, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    auto attributeDescriptions = GetVertexInputAttributeDescriptions(info.vertexInputFormat, &bindingDescription.stride);

    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
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
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
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
    range.size = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &range;
    pipelineLayoutInfo.pSetLayouts = info.sets.data();
    pipelineLayoutInfo.setLayoutCount = (uint32_t)info.sets.size();

    if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create pipeline layout!");
    }

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
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

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_Pipeline) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create graphics pipeline!");
    }

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

  void VulkanPipeline::PushConstants(VkCommandBuffer cmd, glm::mat4& matrix)
  {
    vkCmdPushConstants(
      cmd,
      m_PipelineLayout,
      VK_SHADER_STAGE_ALL,
      0,
      sizeof(glm::mat4),
      &matrix
    );
  }


  VkShaderModule VulkanPipeline::CreateShaderModule(VkDevice device, const std::vector<char>& code)
  {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create shader module!");
    }

    return shaderModule;
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
          throw std::runtime_error("failed to parse vertex input!");
        }
        break;
      }
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
          throw std::runtime_error("unsupported format");
      }

      attributeDescriptions.push_back(attr);
    }

    return attributeDescriptions;
  }

  std::vector<char> VulkanPipeline::ReadFile(std::string_view filename)
  {
    std::string filepath = SHADER_BIN_DIR;
    filepath += '/';
    filepath += filename;
    filepath += ".spv";

    std::ifstream file(filepath, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
      throw std::runtime_error("failed to open file!");
    }

    size_t fileSize = (size_t) file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);

    file.close();

    return buffer;
  }
}
