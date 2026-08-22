#pragma once

#include "Pch.h"
#include "RenderObject.h"

namespace YAEngine
{
  struct FrustumPlane
  {
    glm::vec3 normal;
    float distance;
  };

  // Gribb-Hartmann method: planes are sums and differences of viewProjection rows. The four
  // side planes hold for every projection the engine builds; near and far depend on the depth
  // convention, see the two extractors below.
  inline void ExtractSideFrustumPlanes(const glm::mat4& vp, FrustumPlane planes[4])
  {
    // Left
    planes[0].normal.x = vp[0][3] + vp[0][0];
    planes[0].normal.y = vp[1][3] + vp[1][0];
    planes[0].normal.z = vp[2][3] + vp[2][0];
    planes[0].distance  = vp[3][3] + vp[3][0];

    // Right
    planes[1].normal.x = vp[0][3] - vp[0][0];
    planes[1].normal.y = vp[1][3] - vp[1][0];
    planes[1].normal.z = vp[2][3] - vp[2][0];
    planes[1].distance  = vp[3][3] - vp[3][0];

    // Bottom
    planes[2].normal.x = vp[0][3] + vp[0][1];
    planes[2].normal.y = vp[1][3] + vp[1][1];
    planes[2].normal.z = vp[2][3] + vp[2][1];
    planes[2].distance  = vp[3][3] + vp[3][1];

    // Top
    planes[3].normal.x = vp[0][3] - vp[0][1];
    planes[3].normal.y = vp[1][3] - vp[1][1];
    planes[3].normal.z = vp[2][3] - vp[2][1];
    planes[3].distance  = vp[3][3] - vp[3][1];
  }

  inline void NormalizeFrustumPlanes(FrustumPlane* planes, int planeCount)
  {
    for (int i = 0; i < planeCount; i++)
    {
      float len = glm::length(planes[i].normal);
      planes[i].normal /= len;
      planes[i].distance /= len;
    }
  }

  // Standard-Z projections (shadow cascades, spot and point lights). Near uses the GL-style
  // w + z row (z_ndc >= -1): under the [0,1] depth range that is a conservative plane slightly
  // behind the real near plane, harmless for culling. Far from w - z.
  inline void ExtractFrustumPlanes(const glm::mat4& vp, FrustumPlane planes[6])
  {
    ExtractSideFrustumPlanes(vp, planes);

    // Near
    planes[4].normal.x = vp[0][3] + vp[0][2];
    planes[4].normal.y = vp[1][3] + vp[1][2];
    planes[4].normal.z = vp[2][3] + vp[2][2];
    planes[4].distance  = vp[3][3] + vp[3][2];

    // Far
    planes[5].normal.x = vp[0][3] - vp[0][2];
    planes[5].normal.y = vp[1][3] - vp[1][2];
    planes[5].normal.z = vp[2][3] - vp[2][2];
    planes[5].distance  = vp[3][3] - vp[3][2];

    NormalizeFrustumPlanes(planes, 6);
  }

  // Main camera: reversed-Z with an infinite far plane (Utils/Projection.h). The near plane
  // comes from the reversed clip condition z <= w; the matrix has no far plane at all, so the
  // draw distance is turned into a view-space plane explicitly - without it everything in
  // front of the camera would pass.
  inline void ExtractReversedInfiniteFrustumPlanes(const glm::mat4& view, const glm::mat4& proj,
    float farDistance, FrustumPlane planes[6])
  {
    glm::mat4 vp = proj * view;
    ExtractSideFrustumPlanes(vp, planes);

    // Near: w - z
    planes[4].normal.x = vp[0][3] - vp[0][2];
    planes[4].normal.y = vp[1][3] - vp[1][2];
    planes[4].normal.z = vp[2][3] - vp[2][2];
    planes[4].distance  = vp[3][3] - vp[3][2];

    // Far: the view-space half-space z >= -farDistance, brought to world space the same way
    // the rows of vp are (a plane transforms by the transpose of the point transform).
    glm::vec4 farPlane = glm::transpose(view) * glm::vec4(0.0f, 0.0f, 1.0f, farDistance);
    planes[5].normal = glm::vec3(farPlane);
    planes[5].distance = farPlane.w;

    NormalizeFrustumPlanes(planes, 6);
  }

  inline bool IsAABBVisible(const glm::vec3& bmin, const glm::vec3& bmax, const FrustumPlane* planes, int planeCount = 6)
  {
    for (int i = 0; i < planeCount; i++)
    {
      // P-vertex: the corner of the AABB farthest along the plane normal
      glm::vec3 pVertex {
        planes[i].normal.x >= 0.0f ? bmax.x : bmin.x,
        planes[i].normal.y >= 0.0f ? bmax.y : bmin.y,
        planes[i].normal.z >= 0.0f ? bmax.z : bmin.z
      };

      if (glm::dot(planes[i].normal, pVertex) + planes[i].distance < 0.0f)
        return false;
    }
    return true;
  }

  inline uint32_t FrustumCull(std::vector<RenderObject>& objects, const glm::mat4& view,
    const glm::mat4& proj, float farDistance)
  {
    FrustumPlane planes[6];
    ExtractReversedInfiniteFrustumPlanes(view, proj, farDistance, planes);

    // Partition: visible objects at the front, invisible at the back
    uint32_t visibleEnd = 0;

    for (uint32_t i = 0; i < uint32_t(objects.size()); i++)
    {
      auto& obj = objects[i];

      // No bounds data (sentinel) - always visible
      bool hasBounds = obj.boundsMin.x <= obj.boundsMax.x;
      bool visible = !hasBounds || IsAABBVisible(obj.boundsMin, obj.boundsMax, planes);

      if (visible)
      {
        if (i != visibleEnd)
          std::swap(objects[i], objects[visibleEnd]);
        visibleEnd++;
      }
    }

    return visibleEnd;
  }
}
