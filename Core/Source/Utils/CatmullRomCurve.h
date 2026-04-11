#pragma once

namespace YAEngine
{
  struct CatmullRomCurve
  {
    std::vector<glm::vec2> points;

    float Evaluate(float t) const
    {
      if (points.empty())
        return t;
      if (points.size() == 1)
        return glm::clamp(points[0].y, 0.0f, 1.0f);

      t = glm::clamp(t, 0.0f, 1.0f);

      if (t <= points.front().x)
        return glm::clamp(points.front().y, 0.0f, 1.0f);
      if (t >= points.back().x)
        return glm::clamp(points.back().y, 0.0f, 1.0f);

      size_t seg = 0;
      for (size_t i = 0; i < points.size() - 1; i++)
      {
        if (t >= points[i].x && t <= points[i + 1].x)
        {
          seg = i;
          break;
        }
      }

      float segLen = points[seg + 1].x - points[seg].x;
      float u = (segLen > 0.0f) ? (t - points[seg].x) / segLen : 0.0f;

      glm::vec2 p0 = (seg > 0) ? points[seg - 1] : points[seg];
      glm::vec2 p1 = points[seg];
      glm::vec2 p2 = points[seg + 1];
      glm::vec2 p3 = (seg + 2 < points.size()) ? points[seg + 2] : points[seg + 1];

      float u2 = u * u;
      float u3 = u2 * u;
      float y = 0.5f * (
        (2.0f * p1.y) +
        (-p0.y + p2.y) * u +
        (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * u2 +
        (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * u3
      );

      return glm::clamp(y, 0.0f, 1.0f);
    }

    static CatmullRomCurve Default()
    {
      CatmullRomCurve curve;
      curve.points = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
      return curve;
    }
  };
}
