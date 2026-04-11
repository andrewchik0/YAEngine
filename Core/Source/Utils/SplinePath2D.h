#pragma once

namespace YAEngine
{
  struct SplinePath2D
  {
    std::vector<glm::vec2> points;

    glm::vec2 Evaluate(float t) const
    {
      if (points.empty())
        return glm::vec2(0.5f);
      if (points.size() == 1)
        return points[0];

      t = glm::clamp(t, 0.0f, 1.0f);

      float segCount = static_cast<float>(points.size() - 1);
      float scaled = t * segCount;
      size_t seg = static_cast<size_t>(scaled);
      if (seg >= points.size() - 1)
        seg = points.size() - 2;
      float u = scaled - static_cast<float>(seg);

      glm::vec2 p0 = (seg > 0) ? points[seg - 1] : points[seg];
      glm::vec2 p1 = points[seg];
      glm::vec2 p2 = points[seg + 1];
      glm::vec2 p3 = (seg + 2 < points.size()) ? points[seg + 2] : points[seg + 1];

      float u2 = u * u;
      float u3 = u2 * u;
      return 0.5f * (
        (2.0f * p1) +
        (-p0 + p2) * u +
        (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * u2 +
        (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * u3
      );
    }

    float DistanceTo(glm::vec2 point, int segments = 64) const
    {
      if (points.size() < 2)
        return 1.0f;

      float minDist = std::numeric_limits<float>::max();

      glm::vec2 prev = Evaluate(0.0f);
      for (int i = 1; i <= segments; i++)
      {
        float t = static_cast<float>(i) / static_cast<float>(segments);
        glm::vec2 curr = Evaluate(t);

        glm::vec2 edge = curr - prev;
        float edgeLenSq = glm::dot(edge, edge);
        float proj = 0.0f;
        if (edgeLenSq > 0.0f)
          proj = glm::clamp(glm::dot(point - prev, edge) / edgeLenSq, 0.0f, 1.0f);

        glm::vec2 closest = prev + proj * edge;
        float dist = glm::length(point - closest);
        if (dist < minDist)
          minDist = dist;

        prev = curr;
      }

      return minDist;
    }

    static SplinePath2D Default()
    {
      SplinePath2D path;
      path.points = { { 0.5f, 0.0f }, { 0.5f, 1.0f } };
      return path;
    }
  };
}
