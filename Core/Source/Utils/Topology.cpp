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

  TopologyData Topology::SolidArrow(float shaftRadius, float shaftLength, float tipRadius, float tipLength, uint32_t segments)
  {
    TopologyData data;
    float step = glm::two_pi<float>() / float(segments);

    // Shaft cylinder along -Y: top ring at y=0, bottom ring at y=-shaftLength
    uint32_t shaftTopBase = 0;
    for (uint32_t i = 0; i < segments; ++i)
    {
      float a = step * float(i);
      data.positions.push_back({shaftRadius * glm::cos(a), 0.0f, shaftRadius * glm::sin(a)});
    }

    uint32_t shaftBotBase = segments;
    for (uint32_t i = 0; i < segments; ++i)
    {
      float a = step * float(i);
      data.positions.push_back({shaftRadius * glm::cos(a), -shaftLength, shaftRadius * glm::sin(a)});
    }

    // Shaft side quads
    for (uint32_t i = 0; i < segments; ++i)
    {
      uint32_t next = (i + 1) % segments;
      data.indices.push_back(shaftTopBase + i);
      data.indices.push_back(shaftBotBase + i);
      data.indices.push_back(shaftBotBase + next);

      data.indices.push_back(shaftTopBase + i);
      data.indices.push_back(shaftBotBase + next);
      data.indices.push_back(shaftTopBase + next);
    }

    // Shaft top cap
    uint32_t shaftTopCenter = uint32_t(data.positions.size());
    data.positions.push_back({0.0f, 0.0f, 0.0f});
    for (uint32_t i = 0; i < segments; ++i)
    {
      uint32_t next = (i + 1) % segments;
      data.indices.push_back(shaftTopCenter);
      data.indices.push_back(shaftTopBase + next);
      data.indices.push_back(shaftTopBase + i);
    }

    // Cone tip: base ring at y=-shaftLength, apex at y=-(shaftLength+tipLength)
    uint32_t tipBase = uint32_t(data.positions.size());
    for (uint32_t i = 0; i < segments; ++i)
    {
      float a = step * float(i);
      data.positions.push_back({tipRadius * glm::cos(a), -shaftLength, tipRadius * glm::sin(a)});
    }

    uint32_t apexIdx = uint32_t(data.positions.size());
    data.positions.push_back({0.0f, -(shaftLength + tipLength), 0.0f});

    // Cone side triangles
    for (uint32_t i = 0; i < segments; ++i)
    {
      uint32_t next = (i + 1) % segments;
      data.indices.push_back(tipBase + i);
      data.indices.push_back(apexIdx);
      data.indices.push_back(tipBase + next);
    }

    // Cone base cap
    uint32_t tipCenterIdx = uint32_t(data.positions.size());
    data.positions.push_back({0.0f, -shaftLength, 0.0f});
    for (uint32_t i = 0; i < segments; ++i)
    {
      uint32_t next = (i + 1) % segments;
      data.indices.push_back(tipCenterIdx);
      data.indices.push_back(tipBase + i);
      data.indices.push_back(tipBase + next);
    }

    return data;
  }

  TopologyData Topology::SolidRing(float innerRadius, float outerRadius, uint32_t segments, uint32_t ringSegments)
  {
    TopologyData data;

    float majorStep = glm::two_pi<float>() / float(segments);
    float minorStep = glm::two_pi<float>() / float(ringSegments);
    float tubeRadius = (outerRadius - innerRadius) * 0.5f;
    float centerRadius = innerRadius + tubeRadius;

    data.positions.reserve((segments + 1) * (ringSegments + 1));

    for (uint32_t i = 0; i <= segments; ++i)
    {
      float majorAngle = majorStep * float(i);

      for (uint32_t j = 0; j <= ringSegments; ++j)
      {
        float minorAngle = minorStep * float(j);
        float r = centerRadius + tubeRadius * glm::cos(minorAngle);
        float x = r * glm::cos(majorAngle);
        float y = tubeRadius * glm::sin(minorAngle);
        float z = r * glm::sin(majorAngle);
        data.positions.push_back({x, y, z});
      }
    }

    uint32_t ringVerts = ringSegments + 1;
    for (uint32_t i = 0; i < segments; ++i)
    {
      for (uint32_t j = 0; j < ringSegments; ++j)
      {
        uint32_t a = i * ringVerts + j;
        uint32_t b = a + ringVerts;
        uint32_t c = a + 1;
        uint32_t d = b + 1;

        data.indices.push_back(a);
        data.indices.push_back(b);
        data.indices.push_back(c);

        data.indices.push_back(c);
        data.indices.push_back(b);
        data.indices.push_back(d);
      }
    }

    return data;
  }

  TopologyData Topology::SolidCircle(float radius, uint32_t segments)
  {
    TopologyData data;
    float step = glm::two_pi<float>() / float(segments);

    // Center vertex
    data.positions.push_back({0.0f, 0.0f, 0.0f});

    for (uint32_t i = 0; i < segments; ++i)
    {
      float a = step * float(i);
      data.positions.push_back({radius * glm::cos(a), 0.0f, radius * glm::sin(a)});
    }

    for (uint32_t i = 0; i < segments; ++i)
    {
      uint32_t next = (i + 1) % segments;
      data.indices.push_back(0);
      data.indices.push_back(1 + i);
      data.indices.push_back(1 + next);
    }

    return data;
  }
}
