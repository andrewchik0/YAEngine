#pragma once

#include "Utils/PrimitiveMeshFactory.h"

namespace YAEngine
{
  struct TerrainComponent;

  class TerrainMeshGenerator
  {
  public:
    static ProcMesh Generate(const TerrainComponent& terrain);

  private:
    static float SampleHeight(float x, float z, const TerrainComponent& terrain);
  };
}
