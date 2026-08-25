#pragma once

#include <glm/glm.hpp>

namespace YAEngine
{
  struct QuantizedPositions
  {
    // Four 16-bit components per vertex; the fourth is padding since Vulkan has no
    // three-component 16-bit format, and it keeps every vertex 8-byte aligned.
    std::vector<uint16_t> data;
    // Restored position = bias + scale * value (value normalized to [0,1] by the UNORM
    // format). Folding this pair into the model matrix on the CPU keeps the shader untouched.
    glm::vec3 scale { 0.0f };
    glm::vec3 bias { 0.0f };
    // Worst per-axis deviation in mesh-local units (== world units only at instance scale 1).
    // The same quantized mesh can be shared by instances of different scale, so a caller
    // comparing this against a world-space budget must multiply by the largest scale in use.
    float maxError = 0.0f;
  };

  class PositionQuantizer
  {
  public:

    static constexpr uint32_t STRIDE = uint32_t(4 * sizeof(uint16_t));
    static constexpr uint32_t COMPONENT_MAX = 65535;

    // Packs positions into the mesh's own bounding box; a zero-extent axis collapses to a
    // zero scale so a flat mesh restores exactly instead of dividing by zero.
    static QuantizedPositions Quantize(const glm::vec3* positions, size_t vertexCount);
  };
}
