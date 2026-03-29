#include "Utils/Topology.h"

#include <glm/gtc/constants.hpp>

namespace YAEngine
{
  TopologyData Topology::WireSphere(uint32_t segments)
  {
    TopologyData data;
    data.positions.reserve(segments * 3);
    data.indices.reserve(segments * 6);

    float step = glm::two_pi<float>() / float(segments);

    // XZ plane (y=0)
    uint32_t baseXZ = 0;
    for (uint32_t i = 0; i < segments; ++i)
    {
      float angle = step * float(i);
      data.positions.push_back({glm::cos(angle), 0.0f, glm::sin(angle)});
    }

    // XY plane (z=0)
    uint32_t baseXY = segments;
    for (uint32_t i = 0; i < segments; ++i)
    {
      float angle = step * float(i);
      data.positions.push_back({glm::cos(angle), glm::sin(angle), 0.0f});
    }

    // YZ plane (x=0)
    uint32_t baseYZ = segments * 2;
    for (uint32_t i = 0; i < segments; ++i)
    {
      float angle = step * float(i);
      data.positions.push_back({0.0f, glm::cos(angle), glm::sin(angle)});
    }

    auto pushLoop = [&](uint32_t base)
    {
      for (uint32_t i = 0; i < segments; ++i)
      {
        data.indices.push_back(base + i);
        data.indices.push_back(base + (i + 1) % segments);
      }
    };

    pushLoop(baseXZ);
    pushLoop(baseXY);
    pushLoop(baseYZ);

    return data;
  }

  TopologyData Topology::WireCone(uint32_t segments)
  {
    TopologyData data;

    // Apex + base circle vertices
    data.positions.reserve(1 + segments);
    data.positions.push_back({0.0f, 0.0f, 0.0f});

    float step = glm::two_pi<float>() / float(segments);
    for (uint32_t i = 0; i < segments; ++i)
    {
      float angle = step * float(i);
      data.positions.push_back({glm::cos(angle), -1.0f, glm::sin(angle)});
    }

    uint32_t baseStart = 1;

    // Base circle loop
    for (uint32_t i = 0; i < segments; ++i)
    {
      data.indices.push_back(baseStart + i);
      data.indices.push_back(baseStart + (i + 1) % segments);
    }

    // Lines from apex to every 4th base vertex
    for (uint32_t i = 0; i < segments; i += 4)
    {
      data.indices.push_back(0);
      data.indices.push_back(baseStart + i);
    }

    return data;
  }

  TopologyData Topology::Arrow(uint32_t coneSegments)
  {
    TopologyData data;

    // Shaft: line from origin to (0, -1, 0)
    data.positions.push_back({0.0f, 0.0f, 0.0f});
    data.positions.push_back({0.0f, -1.0f, 0.0f});

    data.indices.push_back(0);
    data.indices.push_back(1);

    // Cone tip: apex at (0,-1,0), base at y=-0.8, radius=0.05
    uint32_t apexIdx = 1; // reuse shaft endpoint

    float step = glm::two_pi<float>() / float(coneSegments);
    uint32_t baseStart = static_cast<uint32_t>(data.positions.size());

    constexpr float baseY = -0.8f;
    constexpr float radius = 0.05f;

    for (uint32_t i = 0; i < coneSegments; ++i)
    {
      float angle = step * float(i);
      data.positions.push_back({radius * glm::cos(angle), baseY, radius * glm::sin(angle)});
    }

    // Base circle loop
    for (uint32_t i = 0; i < coneSegments; ++i)
    {
      data.indices.push_back(baseStart + i);
      data.indices.push_back(baseStart + (i + 1) % coneSegments);
    }

    // Lines from apex to every 4th base vertex
    for (uint32_t i = 0; i < coneSegments; i += 4)
    {
      data.indices.push_back(apexIdx);
      data.indices.push_back(baseStart + i);
    }

    return data;
  }
}
