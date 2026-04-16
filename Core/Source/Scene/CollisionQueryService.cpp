#include "CollisionQueryService.h"

#include "Scene.h"
#include "Components.h"
#include "Utils/Log.h"

namespace YAEngine
{
  static bool OverlapAABBvsAABB(const glm::vec3& aMin, const glm::vec3& aMax,
                                const glm::vec3& bMin, const glm::vec3& bMax)
  {
    return aMin.x <= bMax.x && aMax.x >= bMin.x
        && aMin.y <= bMax.y && aMax.y >= bMin.y
        && aMin.z <= bMax.z && aMax.z >= bMin.z;
  }

  // SAT test from Christer Ericson, "Real-Time Collision Detection" ch. 4.4.1.
  // AABB axes are world basis; OBB axes are the columns of obbAxes.
  static bool OverlapOBBvsAABB(const glm::vec3& obbCenter, const glm::vec3& obbHalf, const glm::mat3& obbAxes,
                               const glm::vec3& aabbCenter, const glm::vec3& aabbHalf)
  {
    // c[i][j] = worldAxis_i . obbAxis_j; glm mat3 is column-major so axes[j] is j-th column
    float c[3][3];
    for (int32_t i = 0; i < 3; i++)
      for (int32_t j = 0; j < 3; j++)
        c[i][j] = obbAxes[j][i];

    // epsilon protects the cross-axis tests when edges are near-parallel
    float absC[3][3];
    const float EPS = 1e-6f;
    for (int32_t i = 0; i < 3; i++)
      for (int32_t j = 0; j < 3; j++)
        absC[i][j] = std::abs(c[i][j]) + EPS;

    const glm::vec3 t = obbCenter - aabbCenter;

    float ra, rb;

    for (int32_t i = 0; i < 3; i++)
    {
      ra = aabbHalf[i];
      rb = obbHalf[0] * absC[i][0] + obbHalf[1] * absC[i][1] + obbHalf[2] * absC[i][2];
      if (std::abs(t[i]) > ra + rb) return false;
    }

    for (int32_t i = 0; i < 3; i++)
    {
      ra = aabbHalf[0] * absC[0][i] + aabbHalf[1] * absC[1][i] + aabbHalf[2] * absC[2][i];
      rb = obbHalf[i];
      if (std::abs(t[0] * c[0][i] + t[1] * c[1][i] + t[2] * c[2][i]) > ra + rb) return false;
    }

    ra = aabbHalf[1] * absC[2][0] + aabbHalf[2] * absC[1][0];
    rb = obbHalf[1] * absC[0][2] + obbHalf[2] * absC[0][1];
    if (std::abs(t[2] * c[1][0] - t[1] * c[2][0]) > ra + rb) return false;

    ra = aabbHalf[1] * absC[2][1] + aabbHalf[2] * absC[1][1];
    rb = obbHalf[0] * absC[0][2] + obbHalf[2] * absC[0][0];
    if (std::abs(t[2] * c[1][1] - t[1] * c[2][1]) > ra + rb) return false;

    ra = aabbHalf[1] * absC[2][2] + aabbHalf[2] * absC[1][2];
    rb = obbHalf[0] * absC[0][1] + obbHalf[1] * absC[0][0];
    if (std::abs(t[2] * c[1][2] - t[1] * c[2][2]) > ra + rb) return false;

    ra = aabbHalf[0] * absC[2][0] + aabbHalf[2] * absC[0][0];
    rb = obbHalf[1] * absC[1][2] + obbHalf[2] * absC[1][1];
    if (std::abs(t[0] * c[2][0] - t[2] * c[0][0]) > ra + rb) return false;

    ra = aabbHalf[0] * absC[2][1] + aabbHalf[2] * absC[0][1];
    rb = obbHalf[0] * absC[1][2] + obbHalf[2] * absC[1][0];
    if (std::abs(t[0] * c[2][1] - t[2] * c[0][1]) > ra + rb) return false;

    ra = aabbHalf[0] * absC[2][2] + aabbHalf[2] * absC[0][2];
    rb = obbHalf[0] * absC[1][1] + obbHalf[1] * absC[1][0];
    if (std::abs(t[0] * c[2][2] - t[2] * c[0][2]) > ra + rb) return false;

    ra = aabbHalf[0] * absC[1][0] + aabbHalf[1] * absC[0][0];
    rb = obbHalf[1] * absC[2][2] + obbHalf[2] * absC[2][1];
    if (std::abs(t[1] * c[0][0] - t[0] * c[1][0]) > ra + rb) return false;

    ra = aabbHalf[0] * absC[1][1] + aabbHalf[1] * absC[0][1];
    rb = obbHalf[0] * absC[2][2] + obbHalf[2] * absC[2][0];
    if (std::abs(t[1] * c[0][1] - t[0] * c[1][1]) > ra + rb) return false;

    ra = aabbHalf[0] * absC[1][2] + aabbHalf[1] * absC[0][2];
    rb = obbHalf[0] * absC[2][1] + obbHalf[1] * absC[2][0];
    if (std::abs(t[1] * c[0][2] - t[0] * c[1][2]) > ra + rb) return false;

    return true;
  }

  std::vector<entt::entity> CollisionQueryService::OverlapAABB(const glm::vec3& worldMin,
                                                               const glm::vec3& worldMax,
                                                               uint32_t mask) const
  {
    // diagnose invalid query AABB so callers can see their own bug in logs
    if (worldMin.x > worldMax.x || worldMin.y > worldMax.y || worldMin.z > worldMax.z)
    {
      YA_LOG_WARN("Physics", "CollisionQueryService::OverlapAABB called with inverted bounds");
      return {};
    }

    static bool s_FirstCall = true;
    if (s_FirstCall)
    {
      YA_LOG_INFO("Physics", "CollisionQueryService active");
      s_FirstCall = false;
    }

    std::vector<entt::entity> result;
    auto& registry = m_Scene.GetRegistry();

    auto singleView = registry.view<ColliderComponent, WorldTransform>();
    for (auto entity : singleView)
    {
      auto& collider = singleView.get<ColliderComponent>(entity);
      if ((collider.layer & mask) == 0) continue;

      auto& wt = singleView.get<WorldTransform>(entity);
      glm::vec3 center = glm::vec3(wt.world * glm::vec4(collider.localOffset, 1.0f));

      // extract scale from the world matrix basis vectors (ignore rotation for MVP)
      glm::vec3 scale(
        glm::length(glm::vec3(wt.world[0])),
        glm::length(glm::vec3(wt.world[1])),
        glm::length(glm::vec3(wt.world[2])));

      glm::vec3 scaledHalf = collider.halfExtents * scale;
      glm::vec3 aMin = center - scaledHalf;
      glm::vec3 aMax = center + scaledHalf;

      if (OverlapAABBvsAABB(aMin, aMax, worldMin, worldMax))
        result.push_back(entity);
    }

    auto instancedView = registry.view<InstancedColliderComponent>();
    for (auto entity : instancedView)
    {
      auto& instanced = instancedView.get<InstancedColliderComponent>(entity);
      if ((instanced.layer & mask) == 0) continue;

      for (auto& entry : instanced.instances)
      {
        glm::vec3 aMin = entry.center - entry.halfExtents;
        glm::vec3 aMax = entry.center + entry.halfExtents;
        if (OverlapAABBvsAABB(aMin, aMax, worldMin, worldMax))
        {
          result.push_back(entity);
          break;
        }
      }
    }

    return result;
  }

  std::vector<entt::entity> CollisionQueryService::OverlapOBB(const glm::vec3& center,
                                                               const glm::vec3& halfExtents,
                                                               const glm::quat& orientation,
                                                               uint32_t mask) const
  {
    if (halfExtents.x < 0.0f || halfExtents.y < 0.0f || halfExtents.z < 0.0f)
    {
      YA_LOG_WARN("Physics", "CollisionQueryService::OverlapOBB called with negative half-extents");
      return {};
    }

    const glm::mat3 axes = glm::mat3_cast(orientation);

    // broad-phase AABB enclosing the OBB
    const glm::vec3 broadHalf(
      std::abs(axes[0].x) * halfExtents.x + std::abs(axes[1].x) * halfExtents.y + std::abs(axes[2].x) * halfExtents.z,
      std::abs(axes[0].y) * halfExtents.x + std::abs(axes[1].y) * halfExtents.y + std::abs(axes[2].y) * halfExtents.z,
      std::abs(axes[0].z) * halfExtents.x + std::abs(axes[1].z) * halfExtents.y + std::abs(axes[2].z) * halfExtents.z);
    const glm::vec3 broadMin = center - broadHalf;
    const glm::vec3 broadMax = center + broadHalf;

    std::vector<entt::entity> result;
    auto& registry = m_Scene.GetRegistry();

    auto singleView = registry.view<ColliderComponent, WorldTransform>();
    for (auto entity : singleView)
    {
      auto& collider = singleView.get<ColliderComponent>(entity);
      if ((collider.layer & mask) == 0) continue;

      auto& wt = singleView.get<WorldTransform>(entity);
      glm::vec3 bCenter = glm::vec3(wt.world * glm::vec4(collider.localOffset, 1.0f));
      glm::vec3 scale(
        glm::length(glm::vec3(wt.world[0])),
        glm::length(glm::vec3(wt.world[1])),
        glm::length(glm::vec3(wt.world[2])));
      glm::vec3 bHalf = collider.halfExtents * scale;

      if (!OverlapAABBvsAABB(broadMin, broadMax, bCenter - bHalf, bCenter + bHalf)) continue;

      if (OverlapOBBvsAABB(center, halfExtents, axes, bCenter, bHalf))
        result.push_back(entity);
    }

    auto instancedView = registry.view<InstancedColliderComponent>();
    for (auto entity : instancedView)
    {
      auto& instanced = instancedView.get<InstancedColliderComponent>(entity);
      if ((instanced.layer & mask) == 0) continue;

      for (auto& entry : instanced.instances)
      {
        glm::vec3 bMin = entry.center - entry.halfExtents;
        glm::vec3 bMax = entry.center + entry.halfExtents;
        if (!OverlapAABBvsAABB(broadMin, broadMax, bMin, bMax)) continue;
        if (OverlapOBBvsAABB(center, halfExtents, axes, entry.center, entry.halfExtents))
        {
          result.push_back(entity);
          break;
        }
      }
    }

    return result;
  }
}
