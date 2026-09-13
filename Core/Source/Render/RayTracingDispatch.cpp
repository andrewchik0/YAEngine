#include "RayTracingDispatch.h"

#include "Utils/Log.h"

namespace YAEngine
{
  namespace
  {
    template<typename T>
    bool LoadEntryPoint(VkDevice device, const char* name, T& target)
    {
      target = reinterpret_cast<T>(vkGetDeviceProcAddr(device, name));
      if (target == nullptr)
        YA_LOG_ERROR("Vulkan", "Ray tracing entry point %s is missing", name);

      return target != nullptr;
    }
  }

  bool RayTracingDispatch::Load(VkDevice device)
  {
    // Bitwise and, not the short-circuiting one: a device missing several entry points
    // should name all of them, not only the first.
    bool complete = true;
    complete &= LoadEntryPoint(device, "vkCreateAccelerationStructureKHR",
      createAccelerationStructure);
    complete &= LoadEntryPoint(device, "vkDestroyAccelerationStructureKHR",
      destroyAccelerationStructure);
    complete &= LoadEntryPoint(device, "vkGetAccelerationStructureBuildSizesKHR",
      getAccelerationStructureBuildSizes);
    complete &= LoadEntryPoint(device, "vkCmdBuildAccelerationStructuresKHR",
      cmdBuildAccelerationStructures);
    complete &= LoadEntryPoint(device, "vkGetAccelerationStructureDeviceAddressKHR",
      getAccelerationStructureDeviceAddress);

    if (!complete)
      *this = {};

    return complete;
  }

  bool RayTracingDispatch::LoadPipeline(VkDevice device)
  {
    bool complete = true;
    complete &= LoadEntryPoint(device, "vkCreateRayTracingPipelinesKHR",
      createRayTracingPipelines);
    complete &= LoadEntryPoint(device, "vkGetRayTracingShaderGroupHandlesKHR",
      getRayTracingShaderGroupHandles);
    complete &= LoadEntryPoint(device, "vkCmdTraceRaysKHR", cmdTraceRays);

    // Only this group is cleared: the acceleration structure entry points are usable on
    // their own, and IsPipelineLoaded is what a shader binding table consumer tests.
    if (!complete)
    {
      createRayTracingPipelines = nullptr;
      getRayTracingShaderGroupHandles = nullptr;
      cmdTraceRays = nullptr;
    }

    return complete;
  }
}
