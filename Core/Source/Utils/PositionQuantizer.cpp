#include "PositionQuantizer.h"

namespace YAEngine
{
  QuantizedPositions PositionQuantizer::Quantize(const glm::vec3* positions, size_t vertexCount)
  {
    QuantizedPositions result;

    if (positions == nullptr || vertexCount == 0)
      return result;

    glm::vec3 boundsMin = positions[0];
    glm::vec3 boundsMax = positions[0];
    for (size_t i = 1; i < vertexCount; i++)
    {
      boundsMin = glm::min(boundsMin, positions[i]);
      boundsMax = glm::max(boundsMax, positions[i]);
    }

    glm::vec3 extent = boundsMax - boundsMin;
    result.bias = boundsMin;
    result.scale = extent;

    glm::vec3 toUnit { 0.0f };
    for (int axis = 0; axis < 3; axis++)
    {
      if (extent[axis] > 0.0f)
        toUnit[axis] = float(COMPONENT_MAX) / extent[axis];
    }

    result.data.resize(vertexCount * 4);

    for (size_t i = 0; i < vertexCount; i++)
    {
      const glm::vec3& source = positions[i];
      for (int axis = 0; axis < 3; axis++)
      {
        float unit = (source[axis] - boundsMin[axis]) * toUnit[axis];
        float rounded = std::clamp(std::round(unit), 0.0f, float(COMPONENT_MAX));
        result.data[i * 4 + axis] = static_cast<uint16_t>(rounded);

        // Measured against what the GPU will actually reconstruct, not against the
        // ideal value, so the rounding of the UNORM divide is part of the number.
        float restored = boundsMin[axis] + extent[axis] * (rounded / float(COMPONENT_MAX));
        result.maxError = std::max(result.maxError, std::abs(restored - source[axis]));
      }
      result.data[i * 4 + 3] = 0;
    }

    return result;
  }
}
