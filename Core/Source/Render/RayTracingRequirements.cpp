#include "RayTracingRequirements.h"

#include "RenderContext.h"
#include "VulkanRequirements.h"
#include "Utils/Log.h"

namespace YAEngine
{
  namespace
  {
    template<typename T>
    const T* FindFeatures(const VulkanRequirements& requirements, VkStructureType type)
    {
      return static_cast<const T*>(requirements.FindDeviceFeatureStruct(type));
    }
  }

  void RegisterRayTracingRequirements(VulkanRequirements& requirements)
  {
    // Dependency order is not cosmetic: acceleration structures may not be enabled without
    // deferred host operations, and the ray tracing pipeline builds on them.
    requirements.AddDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    requirements.AddDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
      { VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME });
    requirements.AddDeviceExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
      { VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME });

    // Value initialization, not a designated initializer list: the requirements merge
    // treats a feature struct as a flat run of VkBool32, so the trailing padding has to be
    // zero or it merges as a set bit.
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationFeatures {};
    accelerationFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    accelerationFeatures.accelerationStructure = VK_TRUE;
    requirements.AddDeviceFeatureStruct(accelerationFeatures, { VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME });

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR pipelineFeatures {};
    pipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    pipelineFeatures.rayTracingPipeline = VK_TRUE;
    requirements.AddDeviceFeatureStruct(pipelineFeatures, { VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME });

    VkPhysicalDeviceVulkan12Features features12 {};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    // Acceleration structure builds address their geometry by device address, and so will
    // every shader that reads vertex data behind a hit.
    features12.bufferDeviceAddress = VK_TRUE;
    // The descriptor indexing set a bindless resource table needs. Nothing consumes it
    // yet, but a shader binding table is useless without one and enabling it later would
    // mean recreating the device.
    features12.descriptorIndexing = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    features12.descriptorBindingPartiallyBound = VK_TRUE;
    features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
    requirements.MergeVulkan12Features(features12);
  }

  void ResolveRayTracingCapabilities(VkPhysicalDevice physicalDevice, const VulkanRequirements& requirements,
                                     RenderContext& context)
  {
    // A feature struct only resolves at all when its extension was enabled, so testing the
    // bit covers both halves of "extension and feature".
    const auto* accelerationFeatures = FindFeatures<VkPhysicalDeviceAccelerationStructureFeaturesKHR>(
      requirements, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR);
    const auto* pipelineFeatures = FindFeatures<VkPhysicalDeviceRayTracingPipelineFeaturesKHR>(
      requirements, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR);

    const VkPhysicalDeviceVulkan12Features& features12 = requirements.GetVulkan12Features();

    const bool accelerationStructure = accelerationFeatures != nullptr
      && accelerationFeatures->accelerationStructure == VK_TRUE;
    const bool rayTracingPipeline = pipelineFeatures != nullptr && pipelineFeatures->rayTracingPipeline == VK_TRUE;
    const bool deferredHostOperations =
      requirements.IsDeviceExtensionEnabled(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

    context.raytracingSupported = accelerationStructure && deferredHostOperations && rayTracingPipeline
      && features12.bufferDeviceAddress == VK_TRUE;
    context.bindlessSupported = features12.descriptorIndexing == VK_TRUE
      && features12.runtimeDescriptorArray == VK_TRUE
      && features12.shaderSampledImageArrayNonUniformIndexing == VK_TRUE
      && features12.descriptorBindingPartiallyBound == VK_TRUE
      && features12.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE
      && features12.descriptorBindingVariableDescriptorCount == VK_TRUE;

    if (context.bindlessSupported)
    {
      VkPhysicalDeviceDescriptorIndexingProperties indexingProperties {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES };
      VkPhysicalDeviceProperties2 properties2 { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                                                .pNext = &indexingProperties };
      vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);

      context.maxUpdateAfterBindSampledImages =
        indexingProperties.maxDescriptorSetUpdateAfterBindSampledImages;
      context.maxPerStageUpdateAfterBindSampledImages =
        indexingProperties.maxPerStageDescriptorUpdateAfterBindSampledImages;
    }

    if (context.raytracingSupported)
    {
      context.rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
      context.accelerationStructureProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
      context.rayTracingPipelineProperties.pNext = &context.accelerationStructureProperties;

      VkPhysicalDeviceProperties2 properties2 { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                                                .pNext = &context.rayTracingPipelineProperties };
      vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);

      // The chain was a query detail. Leaving it in place would give a copy of the context
      // a pointer into the original.
      context.rayTracingPipelineProperties.pNext = nullptr;
    }

    YA_LOG_INFO("Render", "Ray tracing support: hardware ray tracing=%d, bindless descriptors=%d",
      context.raytracingSupported ? 1 : 0,
      context.bindlessSupported ? 1 : 0);

    if (!context.raytracingSupported)
      YA_LOG_WARN("Render", "Hardware ray tracing is unavailable on this device");

    if (!context.bindlessSupported)
      YA_LOG_WARN("Render", "Bindless descriptor indexing is unavailable on this device");
  }
}
