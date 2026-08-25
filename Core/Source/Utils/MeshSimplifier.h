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

    // Deformation each generated level may introduce, in mesh local units. These are
    // world units only at instance scale 1: the mesh is shared by every instance, so
    // the scale it will be drawn at is not known here.
    // Derived from the cascade texel size at the default shadowDistance of 200,
    // roughly 2.9 cm in cascade 1, 6 cm in cascade 2 and 21 cm in cascade 3. The
    // default cascade-to-level map submits level 1 from cascades 1 and 2, so level 1
    // has to respect the tighter cascade 1 texel, while level 2 is submitted by
    // cascade 3 alone. One texel of drift is what the normal bias, texelWorldSize *
    // 1.5, already compensates; anything past that detaches the shadow from its
    // caster and pops at the cascade boundary.
    static constexpr float LEVEL_MAX_ERROR[LOD_COUNT - 1] = { 0.03f, 0.20f };

    // Upper cap on the budget above, as a fraction of the mesh extents. A mesh much
    // larger than a cascade keeps the deformation the purely relative metric used to
    // allow instead of gaining a looser one from the absolute budget.
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

    // Worst deformation any kept level ended up with, the extent of the mesh that
    // paid it, and how many levels the budget threw away. Both errors are in mesh
    // local units, like LEVEL_MAX_ERROR. Without this the number that decides
    // whether the budgets above are safe was not observable anywhere.
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
