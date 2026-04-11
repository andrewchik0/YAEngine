#pragma once

#include "Utils/PrimitiveMeshFactory.h"

namespace YAEngine
{
  struct RoadComponent;

  class RoadMeshGenerator
  {
  public:
    static ProcMesh Generate(const RoadComponent& road);
  };
}
