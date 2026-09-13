#pragma once

#include "Pch.h"

namespace YAEngine
{
  // Which pipeline produces the frame. Raster is the deferred PBR chain; PathTracing keeps
  // its visibility half - depth prepass, G-buffer, velocity - and replaces everything from
  // light culling to the temporal resolve with pt_main.rgen and the resolve of what it wrote.
  //
  // Kept next to AntialiasingMode rather than folded into it: the two stay orthogonal in
  // what they decide - the path picks who shades the frame, the mode picks who resolves it.
  // They touch in exactly one place: ray reconstruction is the path tracer's resolve and it
  // lives inside the DLSS family, so ResolveRenderPath promotes the effective mode to DLAA
  // while the path is on. The user's selection is never rewritten.
  enum class RenderPath : uint32_t
  {
    Raster,
    PathTracing,
    Count
  };

  inline const char* GetRenderPathName(RenderPath path)
  {
    switch (path)
    {
      case RenderPath::Raster: return "Raster";
      case RenderPath::PathTracing: return "Path Tracing";
      case RenderPath::Count: break;
    }

    return "Unknown";
  }
}
