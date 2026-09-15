#pragma once

#include "Pch.h"
#include "PathTraceData.h"

namespace YAEngine
{
  // How the path tracer meets Solid glass on one kind of ray, see PT_GLASS_* in PathTraceData.h.
  enum class PathTraceGlassHandling : uint8_t
  {
    Refract = PT_GLASS_REFRACT,
    Straight = PT_GLASS_STRAIGHT
  };

  // What a path does once ptTransmissionDepth refractive events are spent, see PT_GLASS_OVERFLOW_*.
  enum class PathTraceGlassOverflow : uint8_t
  {
    Terminate = PT_GLASS_OVERFLOW_TERMINATE,
    Straight = PT_GLASS_OVERFLOW_STRAIGHT
  };
}
