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
    static TopologyData WireSphere(uint32_t segments = 32);
    static TopologyData WireCone(uint32_t segments = 32);
    static TopologyData Arrow(uint32_t coneSegments = 16);
  };
}
