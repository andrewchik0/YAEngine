#pragma once

#include "VulkanDebugExtension.h"
#include "Pch.h"
#include "RenderSpecs.h"

namespace YAEngine
{
  class VulkanInstance
  {
  public:
    VulkanDebugExtension m_DebugExtension;

    void Init(const RenderSpecs& specs);
    void Destroy();

    VkInstance& Get()
    {
      return m_Instance;
    }

  private:
    VkInstance m_Instance = nullptr;


    std::vector<const char*> GetGLFWExtensions();
  };
}
