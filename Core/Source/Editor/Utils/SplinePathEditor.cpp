#ifdef YA_EDITOR

#include "Editor/Utils/SplinePathEditor.h"

#include <imgui_internal.h>

#include "Editor/Utils/EditorStyle.h"

namespace YAEngine
{
  namespace
  {
    // Unscaled pixels, multiplied by the window content scale
    constexpr float MAX_CANVAS_SIZE = 260.0f;
    constexpr float POINT_RADIUS = 5.0f;
    constexpr float GRAB_RADIUS = 8.0f;
    constexpr float LINE_THICKNESS = 2.0f;
    constexpr float LABEL_OFFSET = 7.0f;

    constexpr float GRID_ALPHA = 0.5f;
    constexpr int32_t GRID_LINES = 4;
    constexpr int32_t DRAW_SAMPLES = 128;
    constexpr int32_t INSERT_SAMPLES_PER_SEGMENT = 16;

    ImU32 CanvasColor(const glm::vec4& color)
    {
      return ImGui::GetColorU32(ToImGuiColor(color));
    }

    int32_t FindInsertIndex(const std::vector<glm::vec2>& points, glm::vec2 pos)
    {
      if (points.size() < 2)
        return int32_t(points.size());

      SplinePath2D spline { points };
      float minDist = std::numeric_limits<float>::max();
      int32_t bestSegment = 0;
      const int32_t segments = int32_t(points.size() - 1);

      for (int32_t segment = 0; segment < segments; segment++)
      {
        for (int32_t s = 0; s <= INSERT_SAMPLES_PER_SEGMENT; s++)
        {
          const float t = (float(segment) + float(s) / float(INSERT_SAMPLES_PER_SEGMENT)) / float(segments);
          const float dist = glm::length(pos - spline.Evaluate(t));
          if (dist < minDist)
          {
            minDist = dist;
            bestSegment = segment;
          }
        }
      }

      return bestSegment + 1;
    }
  }

  SplinePathEdit SplinePathEditor::Edit(const char* label, std::vector<glm::vec2>& points, ImVec2 size)
  {
    SplinePathEdit edit;
    const float scale = EditorStyle::GetContentScale();
    const EditorTheme& theme = EditorStyle::GetTheme();

    if (size.x <= 0.0f)
    {
      const float side = std::max(std::min(ImGui::GetContentRegionAvail().x, MAX_CANVAS_SIZE * scale), 1.0f);
      size = ImVec2(side, side);
    }
    else if (size.y <= 0.0f)
    {
      size.y = size.x;
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    const auto toScreen = [&](glm::vec2 p) { return ImVec2(origin.x + p.x * size.x, end.y - p.y * size.y); };
    const auto toCanvas = [&](ImVec2 m) {
      return glm::clamp(glm::vec2((m.x - origin.x) / size.x, (end.y - m.y) / size.y), 0.0f, 1.0f);
    };
    const auto findPointNear = [&](ImVec2 m) {
      const float grab = GRAB_RADIUS * scale;
      float bestDistSq = grab * grab;
      int32_t best = -1;
      for (size_t i = 0; i < points.size(); i++)
      {
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

    // The held point lives in the window state, keyed by this canvas, so two canvases never share a drag
    ImGuiStorage* storage = ImGui::GetStateStorage();
    int32_t dragging = storage->GetInt(id, -1);
    bool moved = storage->GetBool(movedId, false);
    // The points can change elsewhere while one is held (the bridge, a Remove button)
    if (dragging >= int32_t(points.size()))
      dragging = -1;

    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
      const glm::vec2 point = toCanvas(mouse);
      const int32_t index = FindInsertIndex(points, point);
      points.insert(points.begin() + index, point);
      edit.insertedIndex = index;
      edit.changed = true;
      edit.committed = true;
      dragging = -1;
    }
    else if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
      dragging = findPointNear(mouse);
      moved = false;
    }
    else if (hovered && dragging < 0 && points.size() > 2 && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
      const int32_t index = findPointNear(mouse);
      if (index >= 0)
      {
        points.erase(points.begin() + index);
        edit.removedIndex = index;
        edit.changed = true;
        edit.committed = true;
      }
    }

    if (dragging >= 0)
    {
      if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
        const glm::vec2 point = toCanvas(mouse);
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
        // A click that did not move the point is no edit
        edit.committed = moved;
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

    if (points.size() >= 2)
    {
      SplinePath2D spline { points };
      const ImU32 lineColor = CanvasColor(theme.accent);
      ImVec2 previous = toScreen(spline.Evaluate(0.0f));
      for (int32_t i = 1; i <= DRAW_SAMPLES; i++)
      {
        const ImVec2 current = toScreen(spline.Evaluate(float(i) / float(DRAW_SAMPLES)));
        draw->AddLine(previous, current, lineColor, LINE_THICKNESS * scale);
        previous = current;
      }
    }

    const ImU32 pointColor = CanvasColor(theme.textPrimary);
    const ImU32 heldColor = CanvasColor(theme.accentHovered);
    const ImU32 indexColor = CanvasColor(theme.textSecondary);
    for (size_t i = 0; i < points.size(); i++)
    {
      const ImVec2 p = toScreen(points[i]);
      char index[16];
      std::snprintf(index, sizeof(index), "%zu", i);
      draw->AddText(ImVec2(p.x + LABEL_OFFSET * scale, p.y - LABEL_OFFSET * scale), indexColor, index);
      draw->AddCircleFilled(p, POINT_RADIUS * scale, int32_t(i) == dragging ? heldColor : pointColor);
    }

    return edit;
  }
}

#endif
