#ifdef YA_EDITOR

#include "Editor/Utils/SplinePathEditor.h"

namespace YAEngine
{
  static int FindInsertIndex(const std::vector<glm::vec2>& points, glm::vec2 pos)
  {
    if (points.size() < 2)
      return static_cast<int>(points.size());

    SplinePath2D spline { points };
    float minDist = std::numeric_limits<float>::max();
    int bestSeg = 0;
    constexpr int SAMPLES_PER_SEG = 16;
    int totalSegments = static_cast<int>(points.size() - 1);

    for (int seg = 0; seg < totalSegments; seg++)
    {
      for (int s = 0; s <= SAMPLES_PER_SEG; s++)
      {
        float t = (static_cast<float>(seg) + static_cast<float>(s) / static_cast<float>(SAMPLES_PER_SEG))
                  / static_cast<float>(totalSegments);
        glm::vec2 p = spline.Evaluate(t);
        float dist = glm::length(pos - p);
        if (dist < minDist)
        {
          minDist = dist;
          bestSeg = seg;
        }
      }
    }

    return bestSeg + 1;
  }

  bool SplinePathEditor::Edit(const char* label, std::vector<glm::vec2>& points, ImVec2 size)
  {
    bool changed = false;

    if (size.x <= 0.0f) size.x = ImGui::GetContentRegionAvail().x;
    if (size.y <= 0.0f) size.y = size.x;

    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_end = ImVec2(canvas_pos.x + size.x, canvas_pos.y + size.y);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Background
    draw->AddRectFilled(canvas_pos, canvas_end, IM_COL32(30, 30, 30, 255));
    draw->AddRect(canvas_pos, canvas_end, IM_COL32(80, 80, 80, 255));

    // Grid
    for (int i = 1; i < 4; i++)
    {
      float t = static_cast<float>(i) * 0.25f;
      float gx = canvas_pos.x + t * size.x;
      float gy = canvas_pos.y + t * size.y;
      draw->AddLine(ImVec2(gx, canvas_pos.y), ImVec2(gx, canvas_end.y), IM_COL32(50, 50, 50, 255));
      draw->AddLine(ImVec2(canvas_pos.x, gy), ImVec2(canvas_end.x, gy), IM_COL32(50, 50, 50, 255));
    }

    // Axis labels
    draw->AddText(ImVec2(canvas_end.x - 14.0f, canvas_end.y - 16.0f), IM_COL32(100, 100, 100, 255), "X");
    draw->AddText(ImVec2(canvas_pos.x + 4.0f, canvas_pos.y + 2.0f), IM_COL32(100, 100, 100, 255), "Z");

    // Draw spline
    if (points.size() >= 2)
    {
      SplinePath2D spline { points };
      ImVec2 prev;
      constexpr int SAMPLES = 128;
      for (int i = 0; i <= SAMPLES; i++)
      {
        float t = static_cast<float>(i) / static_cast<float>(SAMPLES);
        glm::vec2 val = spline.Evaluate(t);
        ImVec2 p = ImVec2(
          canvas_pos.x + val.x * size.x,
          canvas_end.y - val.y * size.y
        );
        if (i > 0)
          draw->AddLine(prev, p, IM_COL32(100, 200, 255, 255), 2.0f);
        prev = p;
      }
    }

    // Invisible button for interaction
    ImGui::SetCursorScreenPos(canvas_pos);
    ImGui::InvisibleButton(label, size);
    bool hovered = ImGui::IsItemHovered();

    // Double-click to add point
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
      ImVec2 mouse = ImGui::GetMousePos();
      float nx = (mouse.x - canvas_pos.x) / size.x;
      float nz = 1.0f - (mouse.y - canvas_pos.y) / size.y;
      nx = glm::clamp(nx, 0.0f, 1.0f);
      nz = glm::clamp(nz, 0.0f, 1.0f);

      glm::vec2 newPt(nx, nz);
      int idx = FindInsertIndex(points, newPt);
      points.insert(points.begin() + idx, newPt);
      changed = true;
    }

    // Draw and interact with control points
    static int s_DraggingIdx = -1;
    static const char* s_DraggingLabel = nullptr;
    constexpr float POINT_RADIUS = 5.0f;
    constexpr float GRAB_RADIUS = 8.0f;

    int removeIdx = -1;

    for (size_t i = 0; i < points.size(); i++)
    {
      ImVec2 pp = ImVec2(
        canvas_pos.x + points[i].x * size.x,
        canvas_end.y - points[i].y * size.y
      );

      ImU32 color = IM_COL32(255, 255, 255, 255);

      // Index label
      char idxBuf[8];
      snprintf(idxBuf, sizeof(idxBuf), "%d", static_cast<int>(i));
      draw->AddText(ImVec2(pp.x + 7.0f, pp.y - 7.0f), IM_COL32(150, 150, 150, 200), idxBuf);

      // Right-click to remove (keep at least 2 points)
      if (hovered && points.size() > 2 && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      {
        ImVec2 mouse = ImGui::GetMousePos();
        float dx = mouse.x - pp.x;
        float dy = mouse.y - pp.y;
        if (dx * dx + dy * dy < GRAB_RADIUS * GRAB_RADIUS)
          removeIdx = static_cast<int>(i);
      }

      // Start drag
      if (hovered && s_DraggingIdx == -1 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
          && !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
      {
        ImVec2 mouse = ImGui::GetMousePos();
        float dx = mouse.x - pp.x;
        float dy = mouse.y - pp.y;
        if (dx * dx + dy * dy < GRAB_RADIUS * GRAB_RADIUS)
        {
          s_DraggingIdx = static_cast<int>(i);
          s_DraggingLabel = label;
        }
      }

      // Active drag
      if (s_DraggingIdx == static_cast<int>(i) && s_DraggingLabel == label)
      {
        color = IM_COL32(255, 200, 50, 255);

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
          ImVec2 mouse = ImGui::GetMousePos();
          float nx = glm::clamp((mouse.x - canvas_pos.x) / size.x, 0.0f, 1.0f);
          float nz = glm::clamp(1.0f - (mouse.y - canvas_pos.y) / size.y, 0.0f, 1.0f);
          points[i] = glm::vec2(nx, nz);
        }
        else
        {
          s_DraggingIdx = -1;
          s_DraggingLabel = nullptr;
          changed = true;
        }
      }

      draw->AddCircleFilled(pp, POINT_RADIUS, color);
    }

    if (removeIdx >= 0)
    {
      points.erase(points.begin() + removeIdx);
      s_DraggingIdx = -1;
      s_DraggingLabel = nullptr;
      changed = true;
    }

    return changed;
  }
}

#endif
