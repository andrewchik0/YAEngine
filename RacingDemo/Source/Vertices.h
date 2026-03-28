#pragma once

#include "Render/VulkanVertexBuffer.h"

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

struct ProcMesh
{
  std::vector<YAEngine::Vertex> vertices;
  std::vector<uint32_t> indices;
};

inline ProcMesh GenerateSphere(
    float radius,
    uint32_t stacks,
    uint32_t slices)
{
  ProcMesh mesh;

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

      mesh.indices.push_back(i0);
      mesh.indices.push_back(i1);
      mesh.indices.push_back(i2);

      mesh.indices.push_back(i1);
      mesh.indices.push_back(i3);
      mesh.indices.push_back(i2);
    }
  }

  return mesh;
}

inline ProcMesh GenerateBox(float sx, float sy, float sz)
{
  ProcMesh mesh;
  float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;

  auto face = [&](glm::vec3 n, glm::vec3 right, glm::vec3 up) {
    uint32_t base = (uint32_t)mesh.vertices.size();
    glm::vec3 center = n * glm::vec3(hx, hy, hz);
    glm::vec3 r = right * glm::vec3(hx, hy, hz);
    glm::vec3 u = up * glm::vec3(hx, hy, hz);

    mesh.vertices.push_back({center - r - u, {0, 0}, n, {right, 1.0f}});
    mesh.vertices.push_back({center + r - u, {1, 0}, n, {right, 1.0f}});
    mesh.vertices.push_back({center + r + u, {1, 1}, n, {right, 1.0f}});
    mesh.vertices.push_back({center - r + u, {0, 1}, n, {right, 1.0f}});

    mesh.indices.push_back(base);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 3);
  };

  face({ 0, 0, 1}, { 1, 0, 0}, {0, 1, 0});  // +Z
  face({ 0, 0,-1}, {-1, 0, 0}, {0, 1, 0});  // -Z
  face({ 1, 0, 0}, { 0, 0,-1}, {0, 1, 0});  // +X
  face({-1, 0, 0}, { 0, 0, 1}, {0, 1, 0});  // -X
  face({ 0, 1, 0}, { 1, 0, 0}, {0, 0,-1});  // +Y
  face({ 0,-1, 0}, { 1, 0, 0}, {0, 0, 1});  // -Y

  return mesh;
}

inline ProcMesh GeneratePlane(float size, float uvScale = 1.0f)
{
  ProcMesh mesh;
  float h = size * 0.5f;
  glm::vec3 n = {0, 1, 0};
  glm::vec4 t = {1, 0, 0, 1};

  mesh.vertices.push_back({{-h, 0, -h}, {0, 0},             n, t});
  mesh.vertices.push_back({{ h, 0, -h}, {uvScale, 0},       n, t});
  mesh.vertices.push_back({{ h, 0,  h}, {uvScale, uvScale},  n, t});
  mesh.vertices.push_back({{-h, 0,  h}, {0, uvScale},       n, t});

  mesh.indices = {0, 2, 1, 0, 3, 2};
  return mesh;
}