#pragma once

#include <glm/glm.hpp>

namespace YAEngine
{
  struct WeldedPositions
  {
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    // False when welding collapsed too few vertices to pay for a second pair of
    // GPU buffers - the caller keeps using the interleaved stream instead.
    bool worthwhile = false;
  };

  class PositionWelder
  {
  public:

    // Pass as worthwhileRatio to always keep the welded arrays - the geometry arena
    // requires every mesh to have a position stream to be batched into a shared buffer.
    static constexpr float KEEP_ALWAYS_RATIO = 2.0f;

    // Deduplicates positions bitwise (no epsilon): merging near-duplicates would let a
    // depth-only pass rasterize them at different subpixel coordinates, cracking shadow maps.
    static WeldedPositions Weld(const glm::vec3* positions, size_t vertexCount,
      const std::vector<uint32_t>& indices, float worthwhileRatio = 0.85f);
  };
}
