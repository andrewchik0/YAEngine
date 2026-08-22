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

    // Deduplicates positions bitwise. An epsilon would merge vertices that a
    // depth-only pass rasterizes at different subpixel coordinates, opening
    // cracks in shadow maps and the depth prepass.
    static WeldedPositions Weld(const glm::vec3* positions, size_t vertexCount,
      const std::vector<uint32_t>& indices, float worthwhileRatio = 0.85f);
  };
}
