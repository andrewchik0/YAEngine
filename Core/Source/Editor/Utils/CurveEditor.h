#pragma once

#ifdef YA_EDITOR

#include <imgui.h>
#include "Utils/CatmullRomCurve.h"

namespace YAEngine
{
  // changed: points were written this frame - a drag step, an insert or a removal.
  // committed: an edit finished this frame - a moved point was released, or a point was inserted or removed.
  struct CurveEdit
  {
    bool changed = false;
    bool committed = false;
    bool active = false;
  };

  namespace CurveEditor
  {
    // Gestures on the canvas, for the tooltip of the row that holds it
    inline constexpr const char* GESTURES = "Drag a point to move it; the end points only move up and down. "
      "Double-click to add a point. Right-click an inner point to remove it.";

    // Curve over normalized 0-1 X and Y, points kept sorted by X. A zero size fills the available width
    // at a fixed height.
    CurveEdit Edit(const char* label, std::vector<glm::vec2>& points, ImVec2 size = ImVec2(0, 0));
  }
}

#endif
