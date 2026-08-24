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

    // Pass as worthwhileRatio to keep the welded arrays no matter how little
    // deduplication saved. The geometry arena needs a position stream for every
    // mesh: a mesh without one cannot be batched into a shared buffer at all.
    static constexpr float KEEP_ALWAYS_RATIO = 2.0f;

    // Deduplicates positions bitwise. An epsilon would merge vertices that a
    // depth-only pass rasterizes at different subpixel coordinates, opening
    // cracks in shadow maps and the depth prepass.
    static WeldedPositions Weld(const glm::vec3* positions, size_t vertexCount,
      const std::vector<uint32_t>& indices, float worthwhileRatio = 0.85f);
  };
}
