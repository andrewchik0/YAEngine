#pragma once

#include "Pch.h"

namespace YAEngine
{
  // Capture limits shared by the bakers, the scene serializer and the editor UI.
  // Kept free of Vulkan and render includes so the scene layer can clamp values on
  // load without pulling the renderer in.
  namespace BakeLimits
  {
    // Reflection probes: all three must stay multiples of TILE_SIZE
    // (Core/Shared/TileCullData.h) so the tile light culling grid divides the
    // capture evenly.
    constexpr uint32_t PROBE_MIN_CAPTURE_RESOLUTION = 64;
    constexpr uint32_t PROBE_MAX_CAPTURE_RESOLUTION = 512;
    constexpr uint32_t PROBE_DEFAULT_CAPTURE_RESOLUTION = 128;

    // Irradiance volumes: an L1 fit averages over the whole hemisphere, so the
    // cube faces stay tiny compared to a probe capture.
    constexpr uint32_t VOLUME_MIN_CAPTURE_RESOLUTION = 8;
    constexpr uint32_t VOLUME_MAX_CAPTURE_RESOLUTION = 64;
    constexpr uint32_t VOLUME_DEFAULT_CAPTURE_RESOLUTION = 32;

    // Every node is a full cube capture, so bake time grows with this directly.
    // Enforced in Render::BakeIrradianceVolume, not only in the details panel -
    // "Bake All Volumes" does not go through the panel.
    constexpr uint32_t VOLUME_MAX_NODE_COUNT = 65536;
  }
}
