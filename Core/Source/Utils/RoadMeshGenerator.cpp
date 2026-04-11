#include "RoadMeshGenerator.h"

#include <entt/entt.hpp>
#include "Scene/Components.h"
#include "Utils/SplinePath3D.h"

namespace YAEngine
{
  ProcMesh RoadMeshGenerator::Generate(const RoadComponent& road)
  {
    ProcMesh mesh;

    if (road.points.size() < 2 || road.segments == 0)
      return mesh;

    uint32_t vertCount = (road.segments + 1) * 2;
    mesh.vertices.reserve(vertCount);
    mesh.indices.reserve(road.segments * 6);

    SplinePath3D spline;
    spline.points = road.points;

    float halfWidth = road.width * 0.5f;
    float accLength = 0.0f;
    glm::vec3 prevPos = spline.Evaluate(0.0f);

    for (uint32_t i = 0; i <= road.segments; i++)
    {
      float t = static_cast<float>(i) / static_cast<float>(road.segments);
      glm::vec3 center = spline.Evaluate(t);
      FrenetFrame frame = spline.ComputeFrenetFrame(t);

      if (i > 0)
        accLength += glm::length(center - prevPos);
      prevPos = center;

      float v = accLength * road.uvScale;

      glm::vec3 left = center - frame.binormal * halfWidth;
      glm::vec3 right = center + frame.binormal * halfWidth;

      glm::vec3 bitangent = glm::cross(frame.normal, frame.tangent);
      float w = glm::dot(bitangent, frame.binormal) > 0.0f ? 1.0f : -1.0f;

      mesh.vertices.push_back({
        .position = left,
        .tex = { 0.0f, v },
        .normal = frame.normal,
        .tangent = glm::vec4(frame.tangent, w)
      });

      mesh.vertices.push_back({
        .position = right,
        .tex = { 1.0f, v },
        .normal = frame.normal,
        .tangent = glm::vec4(frame.tangent, w)
      });
    }

    for (uint32_t i = 0; i < road.segments; i++)
    {
      uint32_t base = i * 2;
      mesh.indices.push_back(base);
      mesh.indices.push_back(base + 1);
      mesh.indices.push_back(base + 2);

      mesh.indices.push_back(base + 1);
      mesh.indices.push_back(base + 3);
      mesh.indices.push_back(base + 2);
    }

    return mesh;
  }
}
