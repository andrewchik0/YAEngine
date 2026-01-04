#include "VulkanDebugExtension.h"

#include <iostream>

namespace YAEngine
{
  VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
  {
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
      std::cerr << "[VULKAN ERROR] " << pCallbackData->pMessage << std::endl;
    }
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
      std::cerr << "[VULKAN WARNING] " << pCallbackData->pMessage << std::endl;
    }
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
    {
      std::cout << "[VULKAN INFO] " << pCallbackData->pMessage << std::endl;
    }
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
    {
      std::cout << "[VULKAN VERBOSE] " << pCallbackData->pMessage << std::endl;
    }
    return VK_FALSE; // VK_TRUE если хочешь прерывать выполнение на ошибке
  }

  VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger)
  {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
      return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    else
    {
      return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
  }

  void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator)
  {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
      func(instance, debugMessenger, pAllocator);
    }
  }

  void VulkanDebugExtension::AddLayer(VkInstanceCreateInfo& info)
  {
    if (!CheckValidationLayerSupport())
    {
      throw std::runtime_error("validation layers requested, but not available!");
    }

    if (!b_Enabled)
    {
      info.enabledLayerCount = 0;
      info.pNext = nullptr;
      return;
    };

    info.enabledLayerCount = 1;
    info.ppEnabledLayerNames = &m_LayerName;

    m_DebugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    m_DebugCreateInfo.flags = 0;
    m_DebugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    m_DebugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    m_DebugCreateInfo.pfnUserCallback = DebugCallback;

    info.pNext = (VkDebugUtilsMessengerCreateInfoEXT*) &m_DebugCreateInfo;
  }

  void VulkanDebugExtension::AddExtension(std::vector<const char*>& extensions) const
  {
    if (b_Enabled)
      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  void VulkanDebugExtension::SetUpMessanger(VkInstance& instance)
  {
    if (!b_Enabled) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.flags = 0;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugCallback;

    if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &m_DebugMessenger) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to set up debug messenger!");
    }
  }

  void VulkanDebugExtension::DestroyMessanger(VkInstance& instance)
  {
    if (b_Enabled)
    {
      DestroyDebugUtilsMessengerEXT(instance, m_DebugMessenger, nullptr);
    }
  }

  bool VulkanDebugExtension::CheckValidationLayerSupport()
  {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    bool layerFound = false;

    for (const auto& layerProperties : availableLayers)
    {
      if (strcmp(m_LayerName, layerProperties.layerName) == 0)
      {
        layerFound = true;
        break;
      }
    }

    if (!layerFound)
    {
      return false;
    }

    return true;
  }
}
