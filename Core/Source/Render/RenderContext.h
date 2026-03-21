#pragma once

#include "Pch.h"

namespace YAEngine
{
  class VulkanCommandBuffer;

  struct RenderContext
  {
    VkDevice device {};
    VmaAllocator allocator {};
    VkQueue graphicsQueue {};
    VulkanCommandBuffer* commandBuffer {};
    VkDescriptorPool descriptorPool {};
    uint32_t maxFramesInFlight {};
  };
}
