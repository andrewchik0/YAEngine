#pragma once

#include "Pch.h"
#include "RayTracingDispatch.h"

namespace YAEngine
{
  class VulkanCommandBuffer;
  class VulkanDescriptorPool;
  class DescriptorLayoutCache;
  class GeometryArena;
  class BindlessTextureRegistry;

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
    // The global bindless texture table every loaded 2D texture registers into, so a hit
    // shader can reach a material's maps from an index alone. Never null on a device that
    // granted the features, and IsValid() is false everywhere else.
    BindlessTextureRegistry* bindlessTextures {};
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
    // Acceleration structures, a ray tracing pipeline and the buffer device addresses both
    // are built from, all granted by the device.
    bool raytracingSupported {};
    // Update-after-bind descriptor arrays indexed non-uniformly, what a bindless resource
    // table needs. Independent of the ray tracing flags.
    bool bindlessSupported {};
    // What one set, and one stage of it, may hold in update-after-bind sampled images.
    // Both cap the bindless table, and both stay zero when bindlessSupported is false.
    uint32_t maxUpdateAfterBindSampledImages {};
    uint32_t maxPerStageUpdateAfterBindSampledImages {};
    // Shader binding table strides, recursion limits and scratch alignments. Left zeroed
    // when raytracingSupported is false, and pNext is deliberately null: the context is a
    // POD that gets copied, a chain pointing into itself would not survive that.
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties {};
    VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationStructureProperties {};
    // The KHR entry points the loader does not export. Zeroed alongside the flags above
    // when the device granted no ray tracing.
    RayTracingDispatch rayTracing {};
  };
}
