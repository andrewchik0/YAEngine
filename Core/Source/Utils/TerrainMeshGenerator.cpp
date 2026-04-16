#include "TerrainMeshGenerator.h"

#include <entt/entt.hpp>
#include "Scene/Components.h"
#include "Utils/HeightmapLoader.h"
#include "Utils/CatmullRomCurve.h"
#include "Utils/SplinePath2D.h"
#include "Utils/SplinePath3D.h"
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
    uint32_t h = HashSeed(params.seed);
    float ox = static_cast<float>(h & 0xFFFF) - 32768.0f;
    float oz = static_cast<float>((h >> 16) & 0xFFFF) - 32768.0f;

    float sx = x;
    float sz = z;
    if (params.warpStrength > 0.0f)
    {
      float wf = params.warpFrequency;
      float wx = glm::simplex(glm::vec2((x + ox) * wf, (z + oz) * wf));
      float wz = glm::simplex(glm::vec2((x + ox + 5.2f) * wf, (z + oz + 1.3f) * wf));
      sx = x + wx * params.warpStrength;
      sz = z + wz * params.warpStrength;
    }

    float amplitude = 1.0f;
    float freq = params.frequency;
    float height = 0.0f;
    float maxAmplitude = 0.0f;
    float weight = 1.0f;

    for (uint32_t i = 0; i < params.octaves; i++)
    {
      float n = glm::simplex(glm::vec2((sx + ox) * freq, (sz + oz) * freq));

      switch (params.noiseType)
      {
        case TerrainNoiseType::Ridged:
        {
          n = 1.0f - std::abs(n);
          n *= n;
          n *= weight;
          weight = glm::clamp(n * 2.0f, 0.0f, 1.0f);
          break;
        }
        case TerrainNoiseType::Billowy:
          n = std::abs(n);
          break;
        default:
          break;
      }

      height += amplitude * n;
      maxAmplitude += amplitude;
      amplitude *= params.persistence;
      freq *= params.lacunarity;
    }

    return height / maxAmplitude;
  }

  float TerrainMeshGenerator::SampleHeightmap(float u, float v, const HeightmapData& hm)
  {
    float px = u * static_cast<float>(hm.width - 1);
    float pz = v * static_cast<float>(hm.height - 1);

    uint32_t x0 = static_cast<uint32_t>(px);
    uint32_t z0 = static_cast<uint32_t>(pz);
    uint32_t x1 = std::min(x0 + 1, hm.width - 1);
    uint32_t z1 = std::min(z0 + 1, hm.height - 1);

    float fx = px - static_cast<float>(x0);
    float fz = pz - static_cast<float>(z0);

    float h00 = hm.heights[z0 * hm.width + x0];
    float h10 = hm.heights[z0 * hm.width + x1];
    float h01 = hm.heights[z1 * hm.width + x0];
    float h11 = hm.heights[z1 * hm.width + x1];

    float h0 = h00 + (h10 - h00) * fx;
    float h1 = h01 + (h11 - h01) * fx;
    return h0 + (h1 - h0) * fz;
  }

  void TerrainMeshGenerator::ComputeNormalsAndTangents(ProcMesh& mesh, uint32_t vertsPerSide,
    float step, const std::vector<float>& heights, const TerrainComponent& params)
  {
    for (uint32_t z = 0; z < vertsPerSide; z++)
    {
      for (uint32_t x = 0; x < vertsPerSide; x++)
      {
        auto getH = [&](uint32_t gx, uint32_t gz) -> float
        {
          gx = std::clamp(gx, 0u, vertsPerSide - 1);
          gz = std::clamp(gz, 0u, vertsPerSide - 1);
          return heights[gz * vertsPerSide + gx];
        };

        float hL = (x > 0) ? getH(x - 1, z) : getH(0, z);
        float hR = (x < params.subdivisions) ? getH(x + 1, z) : getH(params.subdivisions, z);
        float hD = (z > 0) ? getH(x, z - 1) : getH(x, 0);
        float hU = (z < params.subdivisions) ? getH(x, z + 1) : getH(x, params.subdivisions);

        glm::vec3 normal = glm::normalize(glm::vec3(hL - hR, 2.0f * step, hD - hU));
        glm::vec3 tangent = glm::normalize(glm::vec3(2.0f * step, hR - hL, 0.0f));
        glm::vec3 bitangent = glm::normalize(glm::vec3(0.0f, hU - hD, 2.0f * step));
        float w = glm::dot(glm::cross(normal, tangent), bitangent) > 0.0f ? 1.0f : -1.0f;

        auto& vert = mesh.vertices[z * vertsPerSide + x];
        vert.normal = normal;
        vert.tangent = glm::vec4(tangent, w);
      }
    }
  }

  static void GenerateIndices(ProcMesh& mesh, uint32_t subdivisions, uint32_t vertsPerSide)
  {
    for (uint32_t z = 0; z < subdivisions; z++)
    {
      for (uint32_t x = 0; x < subdivisions; x++)
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
  }

  static float ComputeMask(float normX, float normZ, const TerrainComponent& params,
    const SplinePath2D* spline, const CatmullRomCurve* curve)
  {
    if (!spline)
      return 1.0f;

    float dist = spline->DistanceTo(glm::vec2(1.0f - normX, normZ));

    float normalized = (params.maskFalloffRadius > 0.0f)
      ? glm::clamp(dist / params.maskFalloffRadius, 0.0f, 1.0f)
      : (dist > 0.0f ? 1.0f : 0.0f);

    if (curve)
      return curve->Evaluate(normalized);

    return normalized;
  }

  static std::pair<float, float> DistanceAndYToSpline(float wx, float wz,
    const SplinePath3D& spline, int32_t segments = 64)
  {
    float minDistSq = std::numeric_limits<float>::max();
    float bestY = 0.0f;
    glm::vec2 point(wx, wz);

    glm::vec3 prev3d = spline.Evaluate(0.0f);
    glm::vec2 prev(prev3d.x, prev3d.z);
    float prevY = prev3d.y;

    for (int32_t i = 1; i <= segments; i++)
    {
      float t = static_cast<float>(i) / static_cast<float>(segments);
      glm::vec3 curr3d = spline.Evaluate(t);
      glm::vec2 curr(curr3d.x, curr3d.z);
      float currY = curr3d.y;

      glm::vec2 edge = curr - prev;
      float edgeLenSq = glm::dot(edge, edge);
      float proj = 0.0f;
      if (edgeLenSq > 0.0f)
        proj = glm::clamp(glm::dot(point - prev, edge) / edgeLenSq, 0.0f, 1.0f);

      glm::vec2 closest = prev + proj * edge;
      float distSq = glm::dot(point - closest, point - closest);
      if (distSq < minDistSq)
      {
        minDistSq = distSq;
        bestY = glm::mix(prevY, currY, proj);
      }

      prev = curr;
      prevY = currY;
    }

    return { std::sqrt(minDistSq), bestY };
  }

  static float ApplyCarve(float noiseY, float wx, float wz, const TerrainCarveParams* carve)
  {
    if (!carve || carve->outerRadius <= 0.0f || !carve->spline || carve->spline->points.size() < 2)
      return noiseY;

    auto [distXZ, roadY] = DistanceAndYToSpline(wx, wz, *carve->spline);
    float influence = 1.0f - glm::smoothstep(carve->innerRadius, carve->outerRadius, distXZ);

    if (carve->curve)
      influence = carve->curve->Evaluate(influence);

    return glm::mix(noiseY, roadY - carve->depthOffset, influence);
  }

  float TerrainMeshGenerator::SampleSurfaceHeight(float worldX, float worldZ, const TerrainComponent& params)
  {
    if (params.maskPath.empty())
      return SampleHeight(worldX, worldZ, params) * params.heightScale;

    SplinePath2D spline(params.maskPath);
    CatmullRomCurve curve;
    const CatmullRomCurve* curvePtr = params.maskCurve.empty() ? nullptr : &(curve = CatmullRomCurve(params.maskCurve));
    return SampleSurfaceHeight(worldX, worldZ, params, &spline, curvePtr, nullptr);
  }

  float TerrainMeshGenerator::SampleSurfaceHeight(float worldX, float worldZ, const TerrainComponent& params,
    const SplinePath2D* spline, const CatmullRomCurve* curve)
  {
    return SampleSurfaceHeight(worldX, worldZ, params, spline, curve, nullptr);
  }

  float TerrainMeshGenerator::SampleSurfaceHeight(float worldX, float worldZ, const TerrainComponent& params,
    const SplinePath2D* maskSpline, const CatmullRomCurve* maskCurve, const TerrainCarveParams* carve)
  {
    float noise = SampleHeight(worldX, worldZ, params);

    float wy;
    if (!maskSpline)
    {
      wy = noise * params.heightScale;
    }
    else
    {
      float halfSize = params.size * 0.5f;
      float normX = (worldX + halfSize) / params.size;
      float normZ = (worldZ + halfSize) / params.size;
      float mask = ComputeMask(normX, normZ, params, maskSpline, maskCurve);
      wy = (noise * 0.5f + 0.5f) * mask * params.heightScale;
    }

    return ApplyCarve(wy, worldX, worldZ, carve);
  }

  ProcMesh TerrainMeshGenerator::Generate(const TerrainComponent& params)
  {
    return Generate(params, static_cast<const TerrainCarveParams*>(nullptr));
  }

  ProcMesh TerrainMeshGenerator::Generate(const TerrainComponent& params, const TerrainCarveParams* carve)
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

    SplinePath2D spline;
    CatmullRomCurve curve;
    SplinePath2D* splinePtr = nullptr;
    CatmullRomCurve* curvePtr = nullptr;
    if (!params.maskPath.empty())
    {
      spline = SplinePath2D { params.maskPath };
      splinePtr = &spline;
      if (!params.maskCurve.empty())
      {
        curve = CatmullRomCurve { params.maskCurve };
        curvePtr = &curve;
      }
    }

    for (uint32_t z = 0; z < vertsPerSide; z++)
    {
      for (uint32_t x = 0; x < vertsPerSide; x++)
      {
        float normX = static_cast<float>(x) / static_cast<float>(params.subdivisions);
        float normZ = static_cast<float>(z) / static_cast<float>(params.subdivisions);

        float wx = -halfSize + static_cast<float>(x) * step;
        float wz = -halfSize + static_cast<float>(z) * step;

        float noise = SampleHeight(wx, wz, params);
        float mask = ComputeMask(normX, normZ, params, splinePtr, curvePtr);
        float wy;
        if (!params.maskPath.empty())
          wy = (noise * 0.5f + 0.5f) * mask * params.heightScale;
        else
          wy = noise * params.heightScale;

        wy = ApplyCarve(wy, wx, wz, carve);

        heights[z * vertsPerSide + x] = wy;

        float u = normX * params.uvScale;
        float v = normZ * params.uvScale;

        mesh.vertices.push_back({
          .position = { wx, wy, wz },
          .tex = { u, v },
          .normal = { 0.0f, 1.0f, 0.0f },
          .tangent = { 1.0f, 0.0f, 0.0f, 1.0f }
        });
      }
    }

    ComputeNormalsAndTangents(mesh, vertsPerSide, step, heights, params);
    GenerateIndices(mesh, params.subdivisions, vertsPerSide);

    return mesh;
  }

  ProcMesh TerrainMeshGenerator::Generate(const TerrainComponent& params, const HeightmapData& heightmap)
  {
    return Generate(params, heightmap, nullptr);
  }

  ProcMesh TerrainMeshGenerator::Generate(const TerrainComponent& params, const HeightmapData& heightmap,
    const TerrainCarveParams* carve)
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
        float normU = static_cast<float>(x) / static_cast<float>(params.subdivisions);
        float normV = static_cast<float>(z) / static_cast<float>(params.subdivisions);

        float wx = -halfSize + static_cast<float>(x) * step;
        float wz = -halfSize + static_cast<float>(z) * step;
        float wy = SampleHeightmap(normU, normV, heightmap) * params.heightScale;

        wy = ApplyCarve(wy, wx, wz, carve);

        heights[z * vertsPerSide + x] = wy;

        float u = normU * params.uvScale;
        float v = normV * params.uvScale;

        mesh.vertices.push_back({
          .position = { wx, wy, wz },
          .tex = { u, v },
          .normal = { 0.0f, 1.0f, 0.0f },
          .tangent = { 1.0f, 0.0f, 0.0f, 1.0f }
        });
      }
    }

    ComputeNormalsAndTangents(mesh, vertsPerSide, step, heights, params);

    GenerateIndices(mesh, params.subdivisions, vertsPerSide);

    return mesh;
  }
}
