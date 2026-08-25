#pragma once

#include <glm/glm.hpp>

namespace YAEngine
{
  struct MeshLodLevels
  {
    // Index arrays for levels 1..LOD_COUNT-1. An empty entry means simplification
    // could not beat the level above it - the caller must fall back to the
    // nearest populated level.
    std::vector<std::vector<uint32_t>> levels;
  };

  class MeshSimplifier
  {
  public:

    // Level 0 is the source mesh, which is never stored here.
    static constexpr uint32_t LOD_COUNT = 3;

    // Fraction of the source triangle count each generated level aims for.
    static constexpr float LEVEL_RATIOS[LOD_COUNT - 1] = { 0.5f, 0.2f };

    // Deformation budget per level, in mesh-local units (world units only at
    // instance scale 1, since the mesh is shared). Derived from cascade texel size
    // at the default shadowDistance of 200 (~2.9/6/21 cm for cascades 1/2/3): level 1
    // is submitted by cascades 1 and 2 so it must respect the tighter cascade-1
    // texel, while level 2 serves cascade 3 alone. Matches the normal bias tolerance
    // (texelWorldSize * 1.5) - past one texel of drift the shadow detaches from its
    // caster and pops at the cascade boundary.
    static constexpr float LEVEL_MAX_ERROR[LOD_COUNT - 1] = { 0.03f, 0.20f };

    // Upper cap on the budget above, as a fraction of mesh extent - keeps a mesh
    // much larger than a cascade bound by the relative metric instead of being
    // loosened by the absolute per-cascade budget.
    static constexpr float TARGET_ERROR = 0.05f;

    // A level that does not cut at least this much off the previous one is dropped:
    // a near-duplicate index buffer costs arena memory and saves no rasterization.
    static constexpr float WORTHWHILE_RATIO = 0.85f;

    // Below this the fixed cost of an extra draw range outweighs anything a smaller
    // index buffer can save, so the mesh keeps level 0 for every cascade.
    static constexpr size_t MIN_TRIANGLES = 256;

    // Builds simplified index buffers over the welded position stream; positions are
    // untouched since every level indexes the same vertices, so a level costs index
    // memory only.
    static MeshLodLevels Build(const glm::vec3* positions, size_t vertexCount,
      const std::vector<uint32_t>& indices);

    // Worst deformation any kept level ended up with, the extent of the mesh that
    // paid it, and how many levels were dropped over budget (mesh-local units,
    // like LEVEL_MAX_ERROR) - otherwise these numbers are unobservable.
    static float GetMaxKeptError() { return s_MaxKeptError; }
    static float GetMaxKeptErrorExtent() { return s_MaxKeptErrorExtent; }
    static uint32_t GetRejectedLevelCount() { return s_RejectedLevels; }

    static void LogWorstError();

  private:

    static inline float s_MaxKeptError = 0.0f;
    static inline float s_MaxKeptErrorExtent = 0.0f;
    static inline uint32_t s_RejectedLevels = 0;
    static inline bool s_RejectionReported = false;
  };
}
