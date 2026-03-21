#pragma once

#include "Pch.h"

namespace YAEngine
{
  class VulkanCommandBuffer;
  class VulkanDescriptorPool;
  class DescriptorLayoutCache;

  struct RenderContext
  {
    VkDevice device {};
    VmaAllocator allocator {};
    VkQueue graphicsQueue {};
    VulkanCommandBuffer* commandBuffer {};
    VulkanDescriptorPool* descriptorPool {};
    uint32_t maxFramesInFlight {};
    VkPipelineCache pipelineCache {};
    DescriptorLayoutCache* layoutCache {};
  };
}
