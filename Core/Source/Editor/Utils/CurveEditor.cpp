#ifdef YA_EDITOR

#include "Editor/Utils/CurveEditor.h"

#include <imgui_internal.h>

#include "Editor/Utils/EditorStyle.h"

namespace YAEngine
{
  namespace
  {
    // Unscaled pixels, multiplied by the window content scale
    constexpr float CANVAS_HEIGHT = 120.0f;
    constexpr float POINT_RADIUS = 5.0f;
    constexpr float GRAB_RADIUS = 8.0f;
    constexpr float LINE_THICKNESS = 2.0f;

    // Inner points keep off the ends, which stay pinned to X = 0 and X = 1
    constexpr float INNER_MIN_X = 0.01f;
    constexpr float INNER_MAX_X = 0.99f;

    constexpr float GRID_ALPHA = 0.5f;
    constexpr int32_t GRID_LINES = 4;
    constexpr int32_t DRAW_SAMPLES = 128;

    ImU32 CanvasColor(const glm::vec4& color)
    {
      return ImGui::GetColorU32(ToImGuiColor(color));
    }

    void SortByX(std::vector<glm::vec2>& points)
    {
      std::sort(points.begin(), points.end(), [](const glm::vec2& a, const glm::vec2& b) { return a.x < b.x; });
    }
  }

  CurveEdit CurveEditor::Edit(const char* label, std::vector<glm::vec2>& points, ImVec2 size)
  {
    CurveEdit edit;
    const float scale = EditorStyle::GetContentScale();
    const EditorTheme& theme = EditorStyle::GetTheme();

    if (size.x <= 0.0f)
      size.x = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
    if (size.y <= 0.0f)
      size.y = CANVAS_HEIGHT * scale;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    const auto toScreen = [&](glm::vec2 p) { return ImVec2(origin.x + p.x * size.x, end.y - p.y * size.y); };
    const auto findPointNear = [&](ImVec2 m, bool innerOnly) {
      const float grab = GRAB_RADIUS * scale;
      float bestDistSq = grab * grab;
      int32_t best = -1;
      for (size_t i = 0; i < points.size(); i++)
      {
        if (innerOnly && (i == 0 || i + 1 == points.size()))
          continue;
        const ImVec2 p = toScreen(points[i]);
        const float distSq = (m.x - p.x) * (m.x - p.x) + (m.y - p.y) * (m.y - p.y);
        if (distSq < bestDistSq)
        {
          bestDistSq = distSq;
          best = int32_t(i);
        }
      }
      return best;
    };

    ImGui::InvisibleButton(label, size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const ImGuiID id = ImGui::GetItemID();
    const ImGuiID movedId = ImHashStr("##Moved", 0, id);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetMousePos();

    ImGuiStorage* storage = ImGui::GetStateStorage();
    int32_t dragging = storage->GetInt(id, -1);
    bool moved = storage->GetBool(movedId, false);
    if (dragging >= int32_t(points.size()))
      dragging = -1;

    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
      const float x = glm::clamp((mouse.x - origin.x) / size.x, INNER_MIN_X, INNER_MAX_X);
      const float y = glm::clamp((end.y - mouse.y) / size.y, 0.0f, 1.0f);
      points.push_back(glm::vec2(x, y));
      SortByX(points);
      edit.changed = true;
      edit.committed = true;
      dragging = -1;
    }
    else if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
      dragging = findPointNear(mouse, false);
      moved = false;
    }
    else if (hovered && dragging < 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
      const int32_t index = findPointNear(mouse, true);
      if (index >= 0)
      {
        points.erase(points.begin() + index);
        edit.changed = true;
        edit.committed = true;
      }
    }

    if (dragging >= 0)
    {
      if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
        const bool first = dragging == 0;
        const bool last = size_t(dragging) + 1 == points.size();
        const float x = first ? 0.0f : last ? 1.0f : glm::clamp((mouse.x - origin.x) / size.x, INNER_MIN_X, INNER_MAX_X);
        const glm::vec2 point(x, glm::clamp((end.y - mouse.y) / size.y, 0.0f, 1.0f));
        if (point != points[size_t(dragging)])
        {
          points[size_t(dragging)] = point;
          edit.changed = true;
          moved = true;
        }
        edit.active = true;
      }
      else
      {
        // Sorting only on release keeps the held index stable while the point crosses a neighbour
        if (moved)
        {
          SortByX(points);
          edit.committed = true;
        }
        dragging = -1;
        moved = false;
      }
    }
    storage->SetInt(id, dragging);
    storage->SetBool(movedId, moved);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float rounding = theme.radiusSmall * scale;
    draw->AddRectFilled(origin, end, CanvasColor(theme.appBackground), rounding);
    draw->AddRect(origin, end, CanvasColor(theme.borderSubtle), rounding);

    const ImU32 gridColor = CanvasColor(glm::vec4(glm::vec3(theme.borderSubtle), theme.borderSubtle.a * GRID_ALPHA));
    for (int32_t i = 1; i < GRID_LINES; i++)
    {
      const float t = float(i) / float(GRID_LINES);
      const float gx = origin.x + t * size.x;
      const float gy = origin.y + t * size.y;
      draw->AddLine(ImVec2(gx, origin.y), ImVec2(gx, end.y), gridColor);
      draw->AddLine(ImVec2(origin.x, gy), ImVec2(end.x, gy), gridColor);
    }

    CatmullRomCurve curve { points };
    const ImU32 lineColor = CanvasColor(theme.accent);
    ImVec2 previous = toScreen(glm::vec2(0.0f, curve.Evaluate(0.0f)));
    for (int32_t i = 1; i <= DRAW_SAMPLES; i++)
    {
      const float t = float(i) / float(DRAW_SAMPLES);
      const ImVec2 current = toScreen(glm::vec2(t, curve.Evaluate(t)));
      draw->AddLine(previous, current, lineColor, LINE_THICKNESS * scale);
      previous = current;
    }

    const ImU32 pointColor = CanvasColor(theme.textPrimary);
    const ImU32 heldColor = CanvasColor(theme.accentHovered);
    for (size_t i = 0; i < points.size(); i++)
      draw->AddCircleFilled(toScreen(points[i]), POINT_RADIUS * scale, int32_t(i) == dragging ? heldColor : pointColor);

    return edit;
  }
}

#endif
