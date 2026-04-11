#pragma once

#include "Utils/PrimitiveMeshFactory.h"

namespace YAEngine
{
  struct TerrainComponent;
  struct HeightmapData;

  class TerrainMeshGenerator
  {
  public:
    static ProcMesh Generate(const TerrainComponent& terrain);
    static ProcMesh Generate(const TerrainComponent& terrain, const HeightmapData& heightmap);

  private:
    static float SampleHeight(float x, float z, const TerrainComponent& terrain);
    static float SampleHeightmap(float u, float v, const HeightmapData& heightmap);
    static void ComputeNormalsAndTangents(ProcMesh& mesh, uint32_t vertsPerSide, float step,
                                          const std::vector<float>& heights, const TerrainComponent& params);
  };
}
