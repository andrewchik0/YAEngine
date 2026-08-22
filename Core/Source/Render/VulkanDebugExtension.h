#pragma once

#include "Pch.h"

namespace YAEngine
{
  class VulkanDebugExtension
  {
  public:

    const char* m_LayerName = "VK_LAYER_KHRONOS_validation";
    bool b_ValidationLayers = false;
    bool b_DebugUtils = false;

    void Enable(bool validationLayers, bool debugUtils)
    {
      b_ValidationLayers = validationLayers;
      b_DebugUtils = debugUtils;
    }

    void AddLayer(VkInstanceCreateInfo& info);
    void AddExtension(std::vector<const char*>& extensions) const;

    void SetUpMessenger(VkInstance& instance);

    void DestroyMessenger(VkInstance& instance);

  private:
    VkDebugUtilsMessengerEXT m_DebugMessenger {};
    VkDebugUtilsMessengerCreateInfoEXT m_DebugCreateInfo {};

    // Validation reports through the messenger, so debug utils must be on for either flag
    bool NeedsDebugUtils() const { return b_ValidationLayers || b_DebugUtils; }

    bool CheckValidationLayerSupport();
  };
}
