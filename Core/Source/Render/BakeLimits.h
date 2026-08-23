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

    // Every CAPTURED node is a full cube capture, so bake time grows with the count
    // of nodes that survive classification rather than with this. Enforced in
    // Render::BakeIrradianceVolume, not only in the details panel - "Bake All
    // Volumes" does not go through the panel.
    constexpr uint32_t VOLUME_MAX_NODE_COUNT = 131072;

    // Cube face size of the backface classification capture. Deliberately not tied
    // to VOLUME_*_CAPTURE_RESOLUTION: the pass measures a fraction of solid angle,
    // not an image, so it is bound by draw call count rather than by fill rate and
    // there is nothing to gain from more texels.
    constexpr uint32_t VOLUME_BACKFACE_RESOLUTION = 16;

    // Fraction of the sphere that may be covered by geometry facing the node with
    // its inside before the node counts as buried. Nothing can exceed one, so the
    // maximum doubles as the off switch and the baker skips the capture entirely
    // there. Zero rejects a node the moment any surface shows it an inside face.
    constexpr float VOLUME_MIN_BACKFACE_THRESHOLD = 0.0f;
    constexpr float VOLUME_MAX_BACKFACE_THRESHOLD = 1.0f;
    constexpr float VOLUME_DEFAULT_BACKFACE_THRESHOLD = 0.25f;
  }
}
