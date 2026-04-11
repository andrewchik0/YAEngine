#pragma once

#ifdef YA_EDITOR

#include <imgui.h>
#include "Utils/SplinePath2D.h"

namespace YAEngine
{
  namespace SplinePathEditor
  {
    bool Edit(const char* label, std::vector<glm::vec2>& points, ImVec2 size = ImVec2(0, 0));
  }
}

#endif
