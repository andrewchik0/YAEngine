#pragma once

#include "Application.h"

inline std::vector<YAEngine::Vertex> vertices = {
  {
    { -200, -1.0, -200 },
    { 0, 0 },
    { 0, 1, 0 },
    { 0, 0, 1, 1.0 },
  },
  {
    { -200, -1.0, 200 },
    { 0, 50 },
    { 0, 1, 0 },
    { 0, 0, 1, 1.0 },
  },
  {
    { 200, -1.0, -200 },
    { 50, 0 },
    { 0, 1, 0 },
    { 0, 0, 1, 1.0 },
  },
  {
    { 200, -1.0, 200 },
    { 50, 50 },
    { 0, 1, 0 },
    { 0, 0, 1, 1.0 },
  }
};

inline std::vector<uint32_t> indices = {
  0, 1, 2, 1, 3, 2
};

struct SphereMesh
{
  std::vector<YAEngine::Vertex> vertices;
  std::vector<uint32_t> indices;
};

inline SphereMesh GenerateSphere(
    float radius,
    uint32_t stacks,
    uint32_t slices)
{
  SphereMesh mesh;

  for (uint32_t y = 0; y <= stacks; y++)
  {
    float v = (float)y / (float)stacks;
    float phi = v * glm::pi<float>();

    for (uint32_t x = 0; x <= slices; x++)
    {
      float u = (float)x / (float)slices;
      float theta = u * glm::two_pi<float>();

      glm::vec3 normal =
      {
        sin(phi) * cos(theta),
        cos(phi),
        sin(phi) * sin(theta)
    };

      glm::vec3 pos = normal * radius;

      glm::vec3 tangent =
      {
        -sin(theta),
        0.0f,
        cos(theta)
    };

      mesh.vertices.push_back(
      {
          pos,
          { u, 1.0f - v },
          glm::normalize(normal),
          { glm::normalize(tangent), 1.0f }
      });
    }
  }

  uint32_t ringVertexCount = slices + 1;

  for (uint32_t y = 0; y < stacks; y++)
  {
    for (uint32_t x = 0; x < slices; x++)
    {
      uint32_t i0 = y * ringVertexCount + x;
      uint32_t i1 = i0 + 1;
      uint32_t i2 = i0 + ringVertexCount;
      uint32_t i3 = i2 + 1;

      // triangle 1
      mesh.indices.push_back(i0);
      mesh.indices.push_back(i1);
      mesh.indices.push_back(i2);

      // triangle 2
      mesh.indices.push_back(i1);
      mesh.indices.push_back(i3);
      mesh.indices.push_back(i2);
    }
  }

  return mesh;
}