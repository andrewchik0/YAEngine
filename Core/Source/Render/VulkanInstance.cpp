#include "VulkanInstance.h"

#include "Utils/Log.h"

namespace YAEngine
{
  void VulkanInstance::Init(const RenderSpecs& specs, VulkanRequirements& requirements)
  {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = specs.applicationName.c_str();
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Yet Another Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    std::vector<const char*> extensions = GetGLFWExtensions();
    m_DebugExtension.Enable(specs.validationLayers, specs.debugUtils);
    m_DebugExtension.AddExtension(extensions);

    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    extensions.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
    extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);

    if (!requirements.GetInstanceExtensions().empty())
    {
      std::set<std::string> supported = GetSupportedExtensions();
      for (auto& request : requirements.GetInstanceExtensions())
      {
        bool alreadyListed = std::any_of(extensions.begin(), extensions.end(),
          [&](const char* name) { return request.name == name; });

        if (alreadyListed)
        {
          request.enabled = true;
          continue;
        }

        if (!supported.contains(request.name))
        {
          YA_LOG_WARN("Vulkan", "Optional instance extension %s is not supported, skipping it", request.name.c_str());
          continue;
        }

        request.enabled = true;
        extensions.push_back(request.name.c_str());
        YA_LOG_INFO("Vulkan", "Enabling optional instance extension %s", request.name.c_str());
      }
    }

    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    m_DebugExtension.AddLayer(createInfo);

    int32_t result = 0;
    if ((result = vkCreateInstance(&createInfo, nullptr, &m_Instance)) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create Vulkan instance, VkResult = %d", result);
      throw std::runtime_error("Failed to create instance");
    }

    m_DebugExtension.SetUpMessenger(m_Instance);
  }

  void VulkanInstance::Destroy()
  {
    m_DebugExtension.DestroyMessenger(m_Instance);
    vkDestroyInstance(m_Instance, nullptr);
  }

  std::vector<const char*> VulkanInstance::GetGLFWExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    return extensions;
  }

  std::set<std::string> VulkanInstance::GetSupportedExtensions()
  {
    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> available(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, available.data());

    std::set<std::string> names;
    for (const auto& extension : available)
      names.insert(extension.extensionName);

    return names;
  }
}
