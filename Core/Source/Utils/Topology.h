#pragma once

#include <glm/glm.hpp>

namespace YAEngine
{
  struct TopologyData
  {
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
  };

  class Topology
  {
  public:
    static TopologyData WireBox();
    static TopologyData WireSphere(uint32_t segments = 32);
    static TopologyData WireCone(uint32_t segments = 32);
    static TopologyData Arrow(uint32_t coneSegments = 16);

    static TopologyData SolidArrow(float shaftRadius = 0.02f, float shaftLength = 0.8f,
      float tipRadius = 0.06f, float tipLength = 0.2f, uint32_t segments = 16);
    static TopologyData SolidRing(float innerRadius = 0.9f, float outerRadius = 1.0f,
      uint32_t segments = 64, uint32_t ringSegments = 12);
    static TopologyData SolidScaleArrow(float shaftRadius = 0.02f, float shaftLength = 0.8f,
      float cubeHalf = 0.04f, uint32_t segments = 16);
    static TopologyData SolidCircle(float radius = 1.0f, uint32_t segments = 32);
  };
}
