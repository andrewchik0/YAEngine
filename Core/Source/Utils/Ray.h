#pragma once

#include "Pch.h"

namespace YAEngine
{
  struct Ray
  {
    glm::vec3 origin;
    glm::vec3 direction;
  };

  inline Ray ScreenToRay(glm::vec2 normalizedPos, const glm::mat4& invProj, const glm::mat4& invView)
  {
    // Normalized [0,1] -> NDC [-1,1], Y flipped for Vulkan
    float x = normalizedPos.x * 2.0f - 1.0f;
    float y = 1.0f - normalizedPos.y * 2.0f;

    glm::vec4 clipNear(x, y, 0.0f, 1.0f);
    glm::vec4 clipFar(x, y, 1.0f, 1.0f);

    glm::vec4 viewNear = invProj * clipNear;
    glm::vec4 viewFar  = invProj * clipFar;
    viewNear /= viewNear.w;
    viewFar  /= viewFar.w;

    glm::vec3 worldNear = glm::vec3(invView * viewNear);
    glm::vec3 worldFar  = glm::vec3(invView * viewFar);

    return { worldNear, glm::normalize(worldFar - worldNear) };
  }

  inline std::optional<float> RayAABBIntersect(const Ray& ray, const glm::vec3& min, const glm::vec3& max)
  {
    glm::vec3 invDir = 1.0f / ray.direction;
    glm::vec3 t0 = (min - ray.origin) * invDir;
    glm::vec3 t1 = (max - ray.origin) * invDir;

    glm::vec3 tMin = glm::min(t0, t1);
    glm::vec3 tMax = glm::max(t0, t1);

    float tEnter = glm::max(glm::max(tMin.x, tMin.y), tMin.z);
    float tExit  = glm::min(glm::min(tMax.x, tMax.y), tMax.z);

    if (tEnter > tExit || tExit < 0.0f)
      return std::nullopt;

    return tEnter >= 0.0f ? tEnter : tExit;
  }

  inline std::optional<float> RayPlaneIntersect(const Ray& ray, const glm::vec3& planePoint, const glm::vec3& planeNormal)
  {
    float denom = glm::dot(ray.direction, planeNormal);
    if (std::abs(denom) < 1e-6f) return std::nullopt;
    float t = glm::dot(planePoint - ray.origin, planeNormal) / denom;
    if (t < 0.0f) return std::nullopt;
    return t;
  }

  inline std::optional<float> RaySphereIntersect(const Ray& ray, const glm::vec3& center, float radius)
  {
    glm::vec3 oc = ray.origin - center;
    float b = glm::dot(oc, ray.direction);
    float c = glm::dot(oc, oc) - radius * radius;
    float discriminant = b * b - c;

    if (discriminant < 0.0f)
      return std::nullopt;

    float sqrtD = std::sqrt(discriminant);
    float t0 = -b - sqrtD;
    float t1 = -b + sqrtD;

    if (t0 >= 0.0f) return t0;
    if (t1 >= 0.0f) return t1;
    return std::nullopt;
  }
}
