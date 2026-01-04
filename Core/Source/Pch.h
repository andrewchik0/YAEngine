#pragma once

#include <string>
#include <memory>
#include <vector>
#include <stdexcept>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <functional>

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#define VMA_VULKAN_VERSION 1003000
#include "VulkanMemoryAllocator/vk_mem_alloc.h"
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace YAEngine
{
  struct WindowSpecs;
  struct ApplicationSpecs;

  class Window;
  class Application;
}