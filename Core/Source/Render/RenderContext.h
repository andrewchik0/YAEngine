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
    // Needed by the irradiance volume atlas, which packs sub-boxes along X and
    // has to stop before vkCreateImage would fail on the device limit.
    uint32_t maxImageDimension3D {};
  };
}
