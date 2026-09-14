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

    // Fraction of a node's probe rays that may hit the inside of single-sided geometry
    // before the node counts as buried. Nothing can exceed one, so the maximum keeps
    // every node. Zero rejects a node the moment any ray hits an inside face.
    constexpr float VOLUME_MIN_BACKFACE_THRESHOLD = 0.0f;
    constexpr float VOLUME_MAX_BACKFACE_THRESHOLD = 1.0f;
    constexpr float VOLUME_DEFAULT_BACKFACE_THRESHOLD = 0.25f;

    // A baked node nearer to geometry than this fraction of its local spacing - the spacing of the
    // finest brick referencing it - is rejected and dilated like a buried node. So close, a small
    // bright emitter covers a large solid angle of the probe, and trilinear interpolation spreads
    // that near-singular irradiance over the brick. A first value, to be tuned from bake logs.
    constexpr float VOLUME_MIN_PROBE_CLEARANCE_FRACTION = 0.25f;

    // A baked node is also rejected and dilated when at least VOLUME_MAX_ENCLOSED_FRACTION of its
    // rays hit geometry nearer than VOLUME_ENCLOSURE_DISTANCE_FRACTION of its local spacing: it stands
    // inside a closed object, whose inside faces are no back faces when the object is double sided,
    // and records whatever the cavity shows. An open-air node next to a flat wall sees about half its
    // rays hit close geometry, one in a concave corner about three quarters, one inside a hollow
    // column nearly all.
    constexpr float VOLUME_ENCLOSURE_DISTANCE_FRACTION = 1.0f;
    constexpr float VOLUME_MAX_ENCLOSED_FRACTION = 0.9f;

    // Extra clearance in meters by which virtual offset moves a buried node in front of its nearest
    // back face, beyond VOLUME_MIN_PROBE_CLEARANCE_FRACTION of its local spacing, before it is
    // integrated again. The minimum clearance alone is what the too close test demands.
    constexpr float VOLUME_MIN_VIRTUAL_OFFSET_BIAS = 0.005f;
    constexpr float VOLUME_MAX_VIRTUAL_OFFSET_BIAS = 0.5f;
    constexpr float VOLUME_DEFAULT_VIRTUAL_OFFSET_BIAS = 0.05f;

    // Width in meters over which a volume hands its box faces over to the volume enclosing it or to
    // the sky. The default is a quarter of a 4 m brick, soft on a face in open air, yet it pulls the
    // outside only 1 m into a box fitted to the walls of a room.
    constexpr float VOLUME_MIN_EDGE_FADE = 0.05f;
    constexpr float VOLUME_MAX_EDGE_FADE = 8.0f;
    constexpr float VOLUME_DEFAULT_EDGE_FADE = 1.0f;

    // Sparse brick layout of one volume; exceeding any of these fails the layout. A brick is 125
    // texels of three RGBA16F coefficient textures and one R8 validity texture, 25 bytes a texel,
    // so 262144 bricks bound the runtime pool at 262144 x 125 x 25 = 819 MB. Packed bricks share
    // borders, 4^3 unique nodes each, so the node cap is exactly that pool packed: 8.6e9 primary
    // samples at the default 512, about 90 s. Sized for one volume over a whole city block scene
    // at 0.5 m (cafe.scene: about 100k bricks). Indirection cells are 4 bytes, 16 MB at the cap.
    constexpr uint32_t VOLUME_MAX_BRICKS = 1u << 18;
    constexpr uint32_t VOLUME_MAX_UNIQUE_NODES = 1u << 24;
    constexpr uint32_t VOLUME_MAX_INDIRECTION_CELLS = 1u << 22;

    // Rays per point of the placement geometry query. The buried test compares the back-face
    // fraction with thresholds of 0.05 to 0.25, and at 256 rays its standard error stays under 0.03.
    constexpr uint32_t VOLUME_PLACEMENT_QUERY_RAYS = 256;

    // Samples per probe of the volume bake. The ceiling is RT_PROBE_MAX_SAMPLES_PER_PROBE.
    constexpr uint32_t VOLUME_MIN_SAMPLES_PER_PROBE = 16;
    constexpr uint32_t VOLUME_DEFAULT_SAMPLES_PER_PROBE = 512;

    // Nodes x samples per probe above which the details panel warns: about five seconds at the
    // roughly 1e8 primary samples per second volume bakes have logged.
    constexpr uint64_t VOLUME_WARN_PRIMARY_SAMPLES = 500'000'000;

    // Ray traced probe bakes upload, trace and read back points in blocks of at most this many,
    // which is what bounds their buffers: 32 bytes in, 96 out and 96 of readback per point, about
    // 59 MB at the cap. A larger request runs block by block.
    constexpr uint32_t RT_PROBE_MAX_BLOCK_POINTS = 262144;

    // Samples one point traces in one dispatch. A single invocation runs all of them, so this and
    // RT_PROBE_MAX_RAYS_PER_POINT_PASS - not the chunk scheduler - bound how long one point alone
    // can hold the GPU. Samples above either go to further passes.
    constexpr uint32_t RT_PROBE_MAX_SAMPLES_PER_PASS = 4096;

    // Samples per pass a bake requests. The baker lowers it further wherever the ray caps need.
    constexpr uint32_t RT_PROBE_DEFAULT_SAMPLES_PER_PASS = 64;

    // Worst-case rays of one point in one pass. A sample traces its primary ray, a shadow ray per
    // path vertex and a continuation per bounce, 2 * bounces + 2 in all, so 3 bounces pass 1024
    // samples and 8 bounces 455.
    constexpr uint32_t RT_PROBE_MAX_RAYS_PER_POINT_PASS = 8192;

    // Worst-case rays of one submit, enforced whatever the scheduler measured. Light candidate
    // evaluations count toward it too, converted into rays by RayTracedProbeBaker. Even at a
    // pessimistic 5 M fully shaded rays per second that is under a second, half the 2 s Windows
    // TDR. A first guess, to be tuned from bake logs.
    constexpr uint32_t RT_PROBE_MAX_RAYS_PER_SUBMIT = 1u << 22;

    // Samples per probe, and rays per point of a geometry query. Passes are accumulated in double,
    // so time rather than precision bounds this: at the cap one probe traces up to 19 M rays.
    constexpr uint32_t RT_PROBE_MAX_SAMPLES_PER_PROBE = 1u << 20;
  }
}
