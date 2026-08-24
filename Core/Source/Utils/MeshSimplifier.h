#pragma once

#include <glm/glm.hpp>

namespace YAEngine
{
  struct MeshLodLevels
  {
    // Index arrays for levels 1..MeshSimplifier::LOD_COUNT-1, in order. An empty
    // entry means simplification could not beat the level above it, and the caller
    // must fall back to the nearest populated level instead.
    std::vector<std::vector<uint32_t>> levels;
  };

  class MeshSimplifier
  {
  public:

    // Level 0 is the source mesh, which is never stored here.
    static constexpr uint32_t LOD_COUNT = 3;

    // Fraction of the source triangle count each generated level aims for.
    static constexpr float LEVEL_RATIOS[LOD_COUNT - 1] = { 0.5f, 0.2f };

    // Deformation the simplifier may introduce, relative to the mesh extents.
    // Shadow silhouettes tolerate far more than a shaded surface does.
    static constexpr float TARGET_ERROR = 0.05f;

    // A level that does not cut at least this much off the previous one is dropped:
    // a near-duplicate index buffer costs arena memory and saves no rasterization.
    static constexpr float WORTHWHILE_RATIO = 0.85f;

    // Below this the fixed cost of an extra draw range outweighs anything a smaller
    // index buffer can save, so the mesh keeps level 0 for every cascade.
    static constexpr size_t MIN_TRIANGLES = 256;

    // Builds simplified index buffers over the welded position stream. Positions are
    // never touched: every level indexes the same vertices, so a level costs index
    // memory only.
    static MeshLodLevels Build(const glm::vec3* positions, size_t vertexCount,
      const std::vector<uint32_t>& indices);
  };
}
