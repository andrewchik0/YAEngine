#pragma once

#include "Pch.h"

#include "Utils/Log.h"

namespace YAEngine
{

  inline std::vector<char> ReadShaderFile(std::string_view filename)
  {
    std::string filepath = SHADER_BIN_DIR;
    filepath += '/';
    filepath += filename;
    filepath += ".spv";

    std::ifstream file(filepath, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
      YA_LOG_ERROR("Render", "Failed to open shader file: %s", filepath.c_str());
      throw std::runtime_error("failed to open shader file!");
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
  }

  inline VkShaderModule CreateShaderModule(VkDevice device, const std::vector<char>& code)
  {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create shader module");
      throw std::runtime_error("failed to create shader module!");
    }

    return shaderModule;
  }

}
