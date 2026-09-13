#pragma once

#include "Pch.h"

namespace YAEngine
{
  // The Vulkan loader exports no KHR ray tracing entry point, so every one of them has to
  // be fetched from the device. The table is a flat run of function pointers held by value
  // on the RenderContext, which every wrapper already receives: a TLAS builder or a ray
  // tracing pipeline reaches it with no plumbing of its own, and it survives the context
  // being copied where a pointer into the original would not.
  // Stays zeroed when the device granted no ray tracing, so a null entry doubles as the
  // capability test.
  struct RayTracingDispatch
  {
    PFN_vkCreateAccelerationStructureKHR createAccelerationStructure {};
    PFN_vkDestroyAccelerationStructureKHR destroyAccelerationStructure {};
    PFN_vkGetAccelerationStructureBuildSizesKHR getAccelerationStructureBuildSizes {};
    PFN_vkCmdBuildAccelerationStructuresKHR cmdBuildAccelerationStructures {};
    PFN_vkGetAccelerationStructureDeviceAddressKHR getAccelerationStructureDeviceAddress {};
    // Compaction would add vkCmdWriteAccelerationStructuresPropertiesKHR and
    // vkCmdCopyAccelerationStructureKHR here.

    // VK_KHR_ray_tracing_pipeline's own three, loaded separately from the five above and
    // tested separately too. Acceleration structures and ray queries are usable without a
    // shader binding table, so a device that granted the structures but not the pipeline
    // extension keeps everything the TLAS builder and the ray query passes need.
    PFN_vkCreateRayTracingPipelinesKHR createRayTracingPipelines {};
    PFN_vkGetRayTracingShaderGroupHandlesKHR getRayTracingShaderGroupHandles {};
    PFN_vkCmdTraceRaysKHR cmdTraceRays {};

    // False when the device did not hand back every entry point. The caller must then
    // treat ray tracing as unsupported rather than call through a null pointer.
    bool Load(VkDevice device);

    // Same all-or-nothing discipline for the ray tracing pipeline group. Failure clears
    // only those three, so the acceleration structure entry points above survive it.
    bool LoadPipeline(VkDevice device);

    bool IsLoaded() const { return createAccelerationStructure != nullptr; }

    bool IsPipelineLoaded() const { return cmdTraceRays != nullptr; }
  };
}
