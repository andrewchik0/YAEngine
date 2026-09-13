#pragma once

#include "Pch.h"

namespace YAEngine
{
  class VulkanRequirements;
  struct RenderContext;

  // Everything hardware ray tracing needs, registered as optional. A device without it
  // still comes up, only with the capability flags on the context left clear. Must run
  // before the physical device resolves the requirements.
  void RegisterRayTracingRequirements(VulkanRequirements& requirements);

  // Reads back what the device actually granted, fills the ray tracing capability flags
  // and the property limits future subsystems size their buffers from. Must run after
  // device creation.
  void ResolveRayTracingCapabilities(VkPhysicalDevice physicalDevice, const VulkanRequirements& requirements,
                                     RenderContext& context);
}
