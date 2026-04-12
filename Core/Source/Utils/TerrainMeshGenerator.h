#pragma once

#include "Utils/PrimitiveMeshFactory.h"

namespace YAEngine
{
  struct TerrainComponent;
  struct HeightmapData;
  struct SplinePath2D;
  struct CatmullRomCurve;

  class TerrainMeshGenerator
  {
  public:
    static ProcMesh Generate(const TerrainComponent& terrain);
    static ProcMesh Generate(const TerrainComponent& terrain, const HeightmapData& heightmap);
    static float SampleHeight(float x, float z, const TerrainComponent& terrain);
    static float SampleSurfaceHeight(float worldX, float worldZ, const TerrainComponent& terrain);
    static float SampleSurfaceHeight(float worldX, float worldZ, const TerrainComponent& terrain,
                                     const SplinePath2D* spline, const CatmullRomCurve* curve);

  private:
    static float SampleHeightmap(float u, float v, const HeightmapData& heightmap);
    static void ComputeNormalsAndTangents(ProcMesh& mesh, uint32_t vertsPerSide, float step,
                                          const std::vector<float>& heights, const TerrainComponent& params);
  };
}
