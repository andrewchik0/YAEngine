#pragma once

namespace YAEngine
{
  struct FrenetFrame
  {
    glm::vec3 tangent;
    glm::vec3 normal;
    glm::vec3 binormal;
  };

  struct SplinePath3D
  {
    std::vector<glm::vec3> points;

    glm::vec3 Evaluate(float t) const
    {
      if (points.empty())
        return glm::vec3(0.0f);
      if (points.size() == 1)
        return points[0];

      t = glm::clamp(t, 0.0f, 1.0f);

      float segCount = static_cast<float>(points.size() - 1);
      float scaled = t * segCount;
      size_t seg = static_cast<size_t>(scaled);
      if (seg >= points.size() - 1)
        seg = points.size() - 2;
      float u = scaled - static_cast<float>(seg);

      glm::vec3 p0 = (seg > 0) ? points[seg - 1] : points[seg];
      glm::vec3 p1 = points[seg];
      glm::vec3 p2 = points[seg + 1];
      glm::vec3 p3 = (seg + 2 < points.size()) ? points[seg + 2] : points[seg + 1];

      float u2 = u * u;
      float u3 = u2 * u;
      return 0.5f * (
        (2.0f * p1) +
        (-p0 + p2) * u +
        (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * u2 +
        (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * u3
      );
    }

    glm::vec3 EvaluateTangent(float t) const
    {
      if (points.size() < 2)
        return glm::vec3(0.0f, 0.0f, 1.0f);

      t = glm::clamp(t, 0.0f, 1.0f);

      float segCount = static_cast<float>(points.size() - 1);
      float scaled = t * segCount;
      size_t seg = static_cast<size_t>(scaled);
      if (seg >= points.size() - 1)
        seg = points.size() - 2;
      float u = scaled - static_cast<float>(seg);

      glm::vec3 p0 = (seg > 0) ? points[seg - 1] : points[seg];
      glm::vec3 p1 = points[seg];
      glm::vec3 p2 = points[seg + 1];
      glm::vec3 p3 = (seg + 2 < points.size()) ? points[seg + 2] : points[seg + 1];

      float u2 = u * u;
      return 0.5f * (
        (-p0 + p2) +
        (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * 2.0f * u +
        (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * 3.0f * u2
      );
    }

    FrenetFrame ComputeFrenetFrame(float t) const
    {
      glm::vec3 tan = EvaluateTangent(t);
      float len = glm::length(tan);
      if (len < 1e-6f)
        tan = glm::vec3(0.0f, 0.0f, 1.0f);
      else
        tan /= len;

      // Fall back to X axis when tangent is nearly parallel to world up
      glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
      if (glm::abs(glm::dot(tan, up)) > 0.999f)
        up = glm::vec3(1.0f, 0.0f, 0.0f);

      glm::vec3 binormal = glm::normalize(glm::cross(tan, up));
      glm::vec3 normal = glm::cross(binormal, tan);

      return { tan, normal, binormal };
    }

    static SplinePath3D Default()
    {
      SplinePath3D path;
      path.points = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 10.0f } };
      return path;
    }
  };
}
