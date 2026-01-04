#include "VulkanInstance.h"

#include "Application.h"

namespace YAEngine
{
  void VulkanInstance::Init(const RenderSpecs& specs)
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
    if (specs.validationLayers)
    {
      m_DebugExtension.Enable();
    }
    m_DebugExtension.AddExtension(extensions);

    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    extensions.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
    extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);

    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    m_DebugExtension.AddLayer(createInfo);

    int32_t result = 0;
    if ((result = vkCreateInstance(&createInfo, nullptr, &m_Instance)) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create instance!" + result);
    }

    m_DebugExtension.SetUpMessanger(m_Instance);
  }

  void VulkanInstance::Destroy()
  {
    m_DebugExtension.DestroyMessanger(m_Instance);
    vkDestroyInstance(m_Instance, nullptr);
  }

  std::vector<const char*> VulkanInstance::GetGLFWExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    return extensions;
  }
}
