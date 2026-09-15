#pragma once

#ifdef YA_EDITOR

#include <imgui.h>
#include "Utils/SplinePath2D.h"

namespace YAEngine
{
  // changed: points were written this frame - a drag step, an insert or a removal.
  // committed: an edit finished this frame - a moved point was released, or a point was inserted or removed.
  // At most one of insertedIndex and removedIndex is set per frame; both are -1 without a structural change.
  struct SplinePathEdit
  {
    bool changed = false;
    bool committed = false;
    // A point is held: a caller mapping the points onto the canvas keeps that mapping until release
    bool active = false;
    int32_t insertedIndex = -1;
    int32_t removedIndex = -1;
  };

  namespace SplinePathEditor
  {
    // Gestures on the canvas, for the tooltip of the row that holds it
    inline constexpr const char* GESTURES = "Drag a point to move it. Double-click to insert a point on the nearest "
      "segment. Right-click a point to remove it; at least two points stay.";

    // Canvas over normalized 0-1 coordinates, Y up. A zero size gives a square as wide as the available
    // width, capped in height.
    SplinePathEdit Edit(const char* label, std::vector<glm::vec2>& points, ImVec2 size = ImVec2(0, 0));
  }
}

#endif
