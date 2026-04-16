#pragma once

#include "Utils/PrimitiveMeshFactory.h"

namespace YAEngine
{
  struct TerrainComponent;
  struct HeightmapData;
  struct SplinePath2D;
  struct SplinePath3D;
  struct CatmullRomCurve;

  struct TerrainCarveParams
  {
    const SplinePath3D* spline = nullptr;
    float innerRadius = 0.0f;
    float outerRadius = 0.0f;
    float depthOffset = 0.0f;
    const CatmullRomCurve* curve = nullptr;
  };

  class TerrainMeshGenerator
  {
  public:
    static ProcMesh Generate(const TerrainComponent& terrain);
    static ProcMesh Generate(const TerrainComponent& terrain, const TerrainCarveParams* carve);
    static ProcMesh Generate(const TerrainComponent& terrain, const HeightmapData& heightmap);
    static ProcMesh Generate(const TerrainComponent& terrain, const HeightmapData& heightmap,
                             const TerrainCarveParams* carve);
    static float SampleHeight(float x, float z, const TerrainComponent& terrain);
    static float SampleSurfaceHeight(float worldX, float worldZ, const TerrainComponent& terrain);
    static float SampleSurfaceHeight(float worldX, float worldZ, const TerrainComponent& terrain,
                                     const SplinePath2D* spline, const CatmullRomCurve* curve);
    static float SampleSurfaceHeight(float worldX, float worldZ, const TerrainComponent& terrain,
                                     const SplinePath2D* maskSpline, const CatmullRomCurve* maskCurve,
                                     const TerrainCarveParams* carve);

  private:
    static float SampleHeightmap(float u, float v, const HeightmapData& heightmap);
    static void ComputeNormalsAndTangents(ProcMesh& mesh, uint32_t vertsPerSide, float step,
                                          const std::vector<float>& heights, const TerrainComponent& params);
  };
}
