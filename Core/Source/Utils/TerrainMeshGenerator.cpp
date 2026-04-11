#include "TerrainMeshGenerator.h"

#include <entt/entt.hpp>
#include "Scene/Components.h"
#include <glm/gtc/noise.hpp>

namespace YAEngine
{
  static uint32_t HashSeed(int32_t seed)
  {
    uint32_t h = static_cast<uint32_t>(seed);
    h ^= h >> 16;
    h *= 0x45d9f3b;
    h ^= h >> 16;
    h *= 0x45d9f3b;
    h ^= h >> 16;
    return h;
  }

  float TerrainMeshGenerator::SampleHeight(float x, float z, const TerrainComponent& params)
  {
    float amplitude = 1.0f;
    float freq = params.frequency;
    float height = 0.0f;
    float maxAmplitude = 0.0f;

    uint32_t h = HashSeed(params.seed);
    float ox = static_cast<float>(h & 0xFFFF) - 32768.0f;
    float oz = static_cast<float>((h >> 16) & 0xFFFF) - 32768.0f;

    for (uint32_t i = 0; i < params.octaves; i++)
    {
      height += amplitude * glm::simplex(glm::vec2((x + ox) * freq, (z + oz) * freq));
      maxAmplitude += amplitude;
      amplitude *= params.persistence;
      freq *= params.lacunarity;
    }

    return (height / maxAmplitude) * params.heightScale;
  }

  ProcMesh TerrainMeshGenerator::Generate(const TerrainComponent& params)
  {
    ProcMesh mesh;

    uint32_t vertsPerSide = params.subdivisions + 1;
    uint32_t vertexCount = vertsPerSide * vertsPerSide;
    uint32_t quadCount = params.subdivisions * params.subdivisions;

    mesh.vertices.reserve(vertexCount);
    mesh.indices.reserve(quadCount * 6);

    float halfSize = params.size * 0.5f;
    float step = params.size / static_cast<float>(params.subdivisions);

    std::vector<float> heights(vertexCount);

    for (uint32_t z = 0; z < vertsPerSide; z++)
    {
      for (uint32_t x = 0; x < vertsPerSide; x++)
      {
        float wx = -halfSize + static_cast<float>(x) * step;
        float wz = -halfSize + static_cast<float>(z) * step;
        float wy = SampleHeight(wx, wz, params);

        heights[z * vertsPerSide + x] = wy;

        float u = static_cast<float>(x) / static_cast<float>(params.subdivisions) * params.uvScale;
        float v = static_cast<float>(z) / static_cast<float>(params.subdivisions) * params.uvScale;

        mesh.vertices.push_back({
          .position = { wx, wy, wz },
          .tex = { u, v },
          .normal = { 0.0f, 1.0f, 0.0f },
          .tangent = { 1.0f, 0.0f, 0.0f, 1.0f }
        });
      }
    }

    auto getHeight = [&](uint32_t gx, uint32_t gz) -> float
    {
      if (gx < vertsPerSide && gz < vertsPerSide)
        return heights[gz * vertsPerSide + gx];
      float wx = -halfSize + static_cast<float>(gx) * step;
      float wz = -halfSize + static_cast<float>(gz) * step;
      return SampleHeight(wx, wz, params);
    };

    for (uint32_t z = 0; z < vertsPerSide; z++)
    {
      for (uint32_t x = 0; x < vertsPerSide; x++)
      {
        float hL = (x > 0) ? getHeight(x - 1, z) : SampleHeight(-halfSize - step, -halfSize + static_cast<float>(z) * step, params);
        float hR = (x < params.subdivisions) ? getHeight(x + 1, z) : SampleHeight(halfSize + step, -halfSize + static_cast<float>(z) * step, params);
        float hD = (z > 0) ? getHeight(x, z - 1) : SampleHeight(-halfSize + static_cast<float>(x) * step, -halfSize - step, params);
        float hU = (z < params.subdivisions) ? getHeight(x, z + 1) : SampleHeight(-halfSize + static_cast<float>(x) * step, halfSize + step, params);

        glm::vec3 normal = glm::normalize(glm::vec3(hL - hR, 2.0f * step, hD - hU));
        glm::vec3 tangent = glm::normalize(glm::vec3(2.0f * step, hR - hL, 0.0f));

        auto& vert = mesh.vertices[z * vertsPerSide + x];
        vert.normal = normal;
        vert.tangent = glm::vec4(tangent, 1.0f);
      }
    }

    for (uint32_t z = 0; z < params.subdivisions; z++)
    {
      for (uint32_t x = 0; x < params.subdivisions; x++)
      {
        uint32_t i0 = z * vertsPerSide + x;
        uint32_t i1 = i0 + 1;
        uint32_t i2 = i0 + vertsPerSide;
        uint32_t i3 = i2 + 1;

        mesh.indices.push_back(i0);
        mesh.indices.push_back(i2);
        mesh.indices.push_back(i1);

        mesh.indices.push_back(i1);
        mesh.indices.push_back(i2);
        mesh.indices.push_back(i3);
      }
    }

    return mesh;
  }
}
