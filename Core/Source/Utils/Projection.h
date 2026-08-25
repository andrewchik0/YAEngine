#pragma once

#include "Pch.h"

namespace YAEngine
{
  // Reversed-Z perspective, far plane at infinity (right-handed, depth in [0,1], near->1,
  // far->0): puts most float depth precision where 1/z needs it, staying close to uniform
  // in view distance instead of collapsing past a few hundred meters. Hand-built since GLM
  // has no reversed variant; no Y flip - callers apply that to the Vulkan viewport themselves.
  inline glm::mat4 MakeReversedInfinitePerspective(float fovY, float aspect, float nearPlane)
  {
    float f = 1.0f / std::tan(fovY * 0.5f);

    glm::mat4 proj(0.0f);
    proj[0][0] = f / aspect;
    proj[1][1] = f;
    proj[2][3] = -1.0f;
    proj[3][2] = nearPlane;
    return proj;
  }
}
