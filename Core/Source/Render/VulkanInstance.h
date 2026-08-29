#pragma once

#include "VulkanDebugExtension.h"
#include "Pch.h"
#include "RenderSpecs.h"
#include "VulkanRequirements.h"

namespace YAEngine
{
  class VulkanInstance
  {
  public:
    VulkanDebugExtension m_DebugExtension;

    void Init(const RenderSpecs& specs, VulkanRequirements& requirements);
    void Destroy();

    VkInstance& Get()
    {
      return m_Instance;
    }

  private:
    VkInstance m_Instance = nullptr;


    std::vector<const char*> GetGLFWExtensions();
    static std::set<std::string> GetSupportedExtensions();
  };
}
