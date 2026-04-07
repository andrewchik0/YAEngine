#pragma once

#include "Assets/MeshManager.h"
#include "Render/VulkanVertexBuffer.h"

namespace YAEngine
{
  struct ProcMesh
  {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
  };

  enum class PrimitiveType : uint8_t
  {
    Sphere,
    Box,
    Plane
  };

  struct PrimitiveParams
  {
    // Sphere
    float radius = 1.0f;
    uint32_t stacks = 40;
    uint32_t slices = 40;

    // Box
    float sizeX = 2.0f;
    float sizeY = 2.0f;
    float sizeZ = 2.0f;

    // Plane
    float size = 10.0f;
    float uvScale = 1.0f;

    bool operator==(const PrimitiveParams&) const = default;
  };

  struct PrimitiveSource
  {
    PrimitiveType type;
    PrimitiveParams params;
  };

  class PrimitiveMeshFactory
  {
  public:

    void SetDependencies(MeshManager* meshManager) { m_MeshManager = meshManager; }

    MeshHandle Create(PrimitiveType type, const PrimitiveParams& params = {});

    bool GetSource(MeshHandle handle, PrimitiveSource& outSource) const;
    void ClearCache() { m_Cache.clear(); }

    static ProcMesh GenerateSphere(float radius, uint32_t stacks, uint32_t slices);
    static ProcMesh GenerateBox(float sx, float sy, float sz);
    static ProcMesh GeneratePlane(float size, float uvScale = 1.0f);

  private:

    struct CacheKey
    {
      PrimitiveType type;
      PrimitiveParams params;

      bool operator==(const CacheKey&) const = default;
    };

    struct CacheKeyHash
    {
      size_t operator()(const CacheKey& k) const;
    };

    MeshManager* m_MeshManager = nullptr;
    std::unordered_map<CacheKey, MeshHandle, CacheKeyHash> m_Cache;
  };
}
