#pragma once

#include <glm/glm.hpp>

namespace YAEngine
{
  struct QuantizedPositions
  {
    // Four 16-bit components per vertex. The fourth is padding: a three-component
    // 16-bit vertex format is not in the set Vulkan requires, while the four-component
    // one is, and the extra pair of bytes also keeps every vertex 8-byte aligned.
    std::vector<uint16_t> data;
    // A restored position is bias + scale * value, with value already normalized to
    // [0, 1] by the UNORM vertex format. Folding this pair into the model matrix on
    // the CPU is what keeps the shader untouched.
    glm::vec3 scale { 0.0f };
    glm::vec3 bias { 0.0f };
    // Worst per-axis deviation of a restored position from its source, in world
    // units. Carried for diagnostics: it is the number that decides whether 16 bits
    // are enough for a given mesh.
    float maxError = 0.0f;
  };

  class PositionQuantizer
  {
  public:

    static constexpr uint32_t STRIDE = uint32_t(4 * sizeof(uint16_t));
    static constexpr uint32_t COMPONENT_MAX = 65535;

    // Packs positions into the mesh's own bounding box. An axis of zero extent
    // collapses to a zero scale, so a flat mesh restores exactly onto its plane
    // instead of dividing by zero.
    static QuantizedPositions Quantize(const glm::vec3* positions, size_t vertexCount);
  };
}
