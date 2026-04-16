#include "ModelColliderSystem.h"

#include "Components.h"
#include "Assets/AssetManager.h"
#include "Utils/Log.h"

namespace YAEngine
{
  static glm::mat4 ComposeLocal(const LocalTransform& t)
  {
    return glm::translate(glm::mat4(1.0f), t.position)
         * glm::mat4_cast(t.rotation)
         * glm::scale(glm::mat4(1.0f), t.scale);
  }

  static void AccumulateMeshAABB(MeshManager& meshes, MeshHandle handle, const glm::mat4& m,
                                 glm::vec3& outMin, glm::vec3& outMax)
  {
    if (!meshes.Has(handle)) return;
    glm::vec3 minBB = meshes.GetMinBB(handle);
    glm::vec3 maxBB = meshes.GetMaxBB(handle);
    if (minBB.x > maxBB.x || minBB.y > maxBB.y || minBB.z > maxBB.z) return;

    const glm::vec3 corners[8] = {
      { minBB.x, minBB.y, minBB.z }, { maxBB.x, minBB.y, minBB.z },
      { minBB.x, maxBB.y, minBB.z }, { maxBB.x, maxBB.y, minBB.z },
      { minBB.x, minBB.y, maxBB.z }, { maxBB.x, minBB.y, maxBB.z },
      { minBB.x, maxBB.y, maxBB.z }, { maxBB.x, maxBB.y, maxBB.z },
    };
    for (auto& c : corners)
    {
      glm::vec3 wc = glm::vec3(m * glm::vec4(c, 1.0f));
      outMin = glm::min(outMin, wc);
      outMax = glm::max(outMax, wc);
    }
  }

  // root pose is identity because the AABB lives in the root's local space
  static void TraverseModel(entt::registry& registry, MeshManager& meshes, entt::entity entity,
                            const glm::mat4& transformFromRoot, bool isRoot,
                            glm::vec3& outMin, glm::vec3& outMax)
  {
    if (registry.all_of<MeshComponent>(entity))
    {
      auto& mc = registry.get<MeshComponent>(entity);
      AccumulateMeshAABB(meshes, mc.asset, transformFromRoot, outMin, outMax);
    }

    if (!registry.all_of<HierarchyComponent>(entity)) return;

    entt::entity child = registry.get<HierarchyComponent>(entity).firstChild;
    while (child != entt::null)
    {
      glm::mat4 childLocal(1.0f);
      if (registry.all_of<LocalTransform>(child))
        childLocal = ComposeLocal(registry.get<LocalTransform>(child));
      glm::mat4 childFromRoot = transformFromRoot * childLocal;
      TraverseModel(registry, meshes, child, childFromRoot, false, outMin, outMax);
      child = registry.get<HierarchyComponent>(child).nextSibling;
    }
  }

  void ModelColliderSystem::Update(entt::registry& registry, double dt)
  {
    auto view = registry.view<ModelSourceComponent, ModelColliderDirty>();
    std::vector<entt::entity> dirty(view.begin(), view.end());

    for (auto entity : dirty)
    {
      auto& model = registry.get<ModelSourceComponent>(entity);

      if (!model.colliderEnabled)
      {
        // model auto-manages collider when enabled, overwriting any user-added one
        if (registry.all_of<ColliderComponent>(entity))
          registry.remove<ColliderComponent>(entity);
        registry.remove<ModelColliderDirty>(entity);
        continue;
      }

      glm::vec3 localMin( std::numeric_limits<float>::max());
      glm::vec3 localMax(-std::numeric_limits<float>::max());
      TraverseModel(registry, m_Assets.Meshes(), entity, glm::mat4(1.0f), true, localMin, localMax);

      // model meshes may not be loaded yet on the same frame: keep the dirty tag and retry next frame
      if (localMin.x > localMax.x || localMin.y > localMax.y || localMin.z > localMax.z)
        continue;

      glm::vec3 center = (localMin + localMax) * 0.5f + model.colliderOffset;
      glm::vec3 halfExtents = (localMax - localMin) * 0.5f * model.colliderHalfExtentsScale;

      ColliderComponent collider {
        .shape = ColliderShape::AABB,
        .localOffset = center,
        .halfExtents = halfExtents,
        .isStatic = model.colliderIsStatic,
        .layer = model.colliderLayer,
        .mask = model.colliderMask,
      };
      registry.emplace_or_replace<ColliderComponent>(entity, collider);

      YA_LOG_INFO("Physics", "Model collider built for entity %u: half=(%.2f,%.2f,%.2f)",
                  static_cast<uint32_t>(entity),
                  halfExtents.x, halfExtents.y, halfExtents.z);

      registry.remove<ModelColliderDirty>(entity);
    }
  }
}
