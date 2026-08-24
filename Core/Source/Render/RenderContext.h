#pragma once

#include "Pch.h"

namespace YAEngine
{
  class VulkanCommandBuffer;
  class VulkanDescriptorPool;
  class DescriptorLayoutCache;
  class GeometryArena;

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
    // Shared position and index storage for every mesh, so depth-only passes can
    // bind geometry once instead of once per draw.
    GeometryArena* geometryArena {};
    // Needed by the irradiance volume atlas, which packs sub-boxes along X and
    // has to stop before vkCreateImage would fail on the device limit.
    uint32_t maxImageDimension3D {};
    // Nanoseconds per timestamp tick, how many low bits of a timestamp are actually
    // meaningful on the graphics queue, and whether timestamps work here at all.
    float timestampPeriod {};
    uint32_t timestampValidBits {};
    bool timestampsSupported {};
    // Shadow pipelines drop near/far clipping in favour of depth clamping, which needs
    // this optional device feature.
    bool depthClampSupported {};
    // Batched shadow casters need both: more than one command per
    // vkCmdDrawIndexedIndirect, and a non-zero firstInstance inside a command to
    // pick the model matrix out of a shared array.
    bool multiDrawIndirectSupported {};
    bool drawIndirectFirstInstanceSupported {};
    bool unorm16VertexSupported {};
    uint32_t maxDrawIndirectCount {};
  };
}
