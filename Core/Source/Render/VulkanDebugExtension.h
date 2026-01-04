#pragma once

#include "Pch.h"

namespace YAEngine
{
  class VulkanDebugExtension
  {
  public:

    const char* m_LayerName = "VK_LAYER_KHRONOS_validation";
    bool b_Enabled = false;

    void Enable()
    {
      b_Enabled = true;
    }

    void AddLayer(VkInstanceCreateInfo& info);
    void AddExtension(std::vector<const char*>& extensions) const;

    void SetUpMessanger(VkInstance& instance);

    void DestroyMessanger(VkInstance& instance);

  private:
    VkDebugUtilsMessengerEXT m_DebugMessenger {};
    VkDebugUtilsMessengerCreateInfoEXT m_DebugCreateInfo {};

    bool CheckValidationLayerSupport();
  };
}