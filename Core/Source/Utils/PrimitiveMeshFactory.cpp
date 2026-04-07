#include "PrimitiveMeshFactory.h"

namespace YAEngine
{
  MeshHandle PrimitiveMeshFactory::Create(PrimitiveType type, const PrimitiveParams& params)
  {
    CacheKey key { type, params };
    auto it = m_Cache.find(key);
    if (it != m_Cache.end() && m_MeshManager->Has(it->second))
      return it->second;

    ProcMesh mesh;
    switch (type)
    {
      case PrimitiveType::Sphere:
        mesh = GenerateSphere(params.radius, params.stacks, params.slices);
        break;
      case PrimitiveType::Box:
        mesh = GenerateBox(params.sizeX, params.sizeY, params.sizeZ);
        break;
      case PrimitiveType::Plane:
        mesh = GeneratePlane(params.size, params.uvScale);
        break;
    }

    auto handle = m_MeshManager->Load(mesh.vertices, mesh.indices);
    m_Cache[key] = handle;
    return handle;
  }

  bool PrimitiveMeshFactory::GetSource(MeshHandle handle, PrimitiveSource& outSource) const
  {
    for (auto& [key, cached] : m_Cache)
    {
      if (cached == handle)
      {
        outSource.type = key.type;
        outSource.params = key.params;
        return true;
      }
    }
    return false;
  }

  ProcMesh PrimitiveMeshFactory::GenerateSphere(float radius, uint32_t stacks, uint32_t slices)
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

        mesh.vertices.push_back({
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

  ProcMesh PrimitiveMeshFactory::GenerateBox(float sx, float sy, float sz)
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

    face({ 0, 0, 1}, { 1, 0, 0}, {0, 1, 0});
    face({ 0, 0,-1}, {-1, 0, 0}, {0, 1, 0});
    face({ 1, 0, 0}, { 0, 0,-1}, {0, 1, 0});
    face({-1, 0, 0}, { 0, 0, 1}, {0, 1, 0});
    face({ 0, 1, 0}, { 1, 0, 0}, {0, 0,-1});
    face({ 0,-1, 0}, { 1, 0, 0}, {0, 0, 1});

    return mesh;
  }

  ProcMesh PrimitiveMeshFactory::GeneratePlane(float size, float uvScale)
  {
    ProcMesh mesh;
    float h = size * 0.5f;
    glm::vec3 n = {0, 1, 0};
    glm::vec4 t = {1, 0, 0, 1};

    mesh.vertices.push_back({{-h, 0, -h}, {0, 0},             n, t});
    mesh.vertices.push_back({{ h, 0, -h}, {uvScale, 0},       n, t});
    mesh.vertices.push_back({{ h, 0,  h}, {uvScale, uvScale}, n, t});
    mesh.vertices.push_back({{-h, 0,  h}, {0, uvScale},       n, t});

    mesh.indices = {0, 2, 1, 0, 3, 2};
    return mesh;
  }

  size_t PrimitiveMeshFactory::CacheKeyHash::operator()(const CacheKey& k) const
  {
    size_t h = std::hash<uint8_t>()(static_cast<uint8_t>(k.type));
    auto combine = [&h](auto val) {
      h ^= std::hash<decltype(val)>()(val) + 0x9e3779b9 + (h << 6) + (h >> 2);
    };
    combine(k.params.radius);
    combine(k.params.stacks);
    combine(k.params.slices);
    combine(k.params.sizeX);
    combine(k.params.sizeY);
    combine(k.params.sizeZ);
    combine(k.params.size);
    combine(k.params.uvScale);
    return h;
  }
}
