#pragma once

#include "Pch.h"

namespace YAEngine
{
  // Reversed-Z perspective with the far plane at infinity (right-handed, depth in [0,1],
  // near maps to 1 and infinity to 0). A float depth buffer spends most of its precision
  // near zero, which is exactly where 1/z piles up distant geometry, so this distribution
  // is close to uniform in view distance instead of collapsing past a few hundred meters.
  // GLM has no reversed variant, hence the hand-built matrix. No Y flip - callers that
  // feed Vulkan viewports apply it themselves.
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
