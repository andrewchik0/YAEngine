#ifdef YA_EDITOR

#include "Editor/Utils/CurveEditor.h"

namespace YAEngine
{
  bool CurveEditor::Edit(const char* label, std::vector<glm::vec2>& points, ImVec2 size)
  {
    bool changed = false;

    if (size.x <= 0.0f) size.x = ImGui::GetContentRegionAvail().x;
    if (size.y <= 0.0f) size.y = 150.0f;

    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_end = ImVec2(canvas_pos.x + size.x, canvas_pos.y + size.y);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Background
    draw->AddRectFilled(canvas_pos, canvas_end, IM_COL32(30, 30, 30, 255));
    draw->AddRect(canvas_pos, canvas_end, IM_COL32(80, 80, 80, 255));

    // Grid lines at 0.25, 0.5, 0.75
    for (int i = 1; i < 4; i++)
    {
      float t = static_cast<float>(i) * 0.25f;
      float gx = canvas_pos.x + t * size.x;
      float gy = canvas_pos.y + (1.0f - t) * size.y;
      draw->AddLine(ImVec2(gx, canvas_pos.y), ImVec2(gx, canvas_end.y), IM_COL32(50, 50, 50, 255));
      draw->AddLine(ImVec2(canvas_pos.x, gy), ImVec2(canvas_end.x, gy), IM_COL32(50, 50, 50, 255));
    }

    // Draw curve by sampling
    CatmullRomCurve curve { points };
    ImVec2 prev;
    constexpr int SAMPLES = 128;
    for (int i = 0; i <= SAMPLES; i++)
    {
      float t = static_cast<float>(i) / static_cast<float>(SAMPLES);
      float val = curve.Evaluate(t);
      ImVec2 p = ImVec2(
        canvas_pos.x + t * size.x,
        canvas_pos.y + (1.0f - val) * size.y
      );
      if (i > 0)
        draw->AddLine(prev, p, IM_COL32(200, 200, 100, 255), 2.0f);
      prev = p;
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
      float ny = 1.0f - (mouse.y - canvas_pos.y) / size.y;
      nx = glm::clamp(nx, 0.01f, 0.99f);
      ny = glm::clamp(ny, 0.0f, 1.0f);

      points.push_back(glm::vec2(nx, ny));
      std::sort(points.begin(), points.end(),
        [](const glm::vec2& a, const glm::vec2& b) { return a.x < b.x; });
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
        canvas_pos.y + (1.0f - points[i].y) * size.y
      );

      bool isFirst = (i == 0);
      bool isLast = (i == points.size() - 1);

      ImU32 color = IM_COL32(255, 255, 255, 255);

      // Right-click to remove (except first/last)
      if (hovered && !isFirst && !isLast && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
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
          float nx = (mouse.x - canvas_pos.x) / size.x;
          float ny = 1.0f - (mouse.y - canvas_pos.y) / size.y;

          // First/last points locked on X
          if (isFirst) nx = 0.0f;
          else if (isLast) nx = 1.0f;
          else nx = glm::clamp(nx, 0.01f, 0.99f);

          ny = glm::clamp(ny, 0.0f, 1.0f);

          points[i] = glm::vec2(nx, ny);
        }
        else
        {
          s_DraggingIdx = -1;
          s_DraggingLabel = nullptr;

          std::sort(points.begin(), points.end(),
            [](const glm::vec2& a, const glm::vec2& b) { return a.x < b.x; });
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
