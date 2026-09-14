#ifdef YA_EDITOR

#include "BakeExclusion.h"

namespace YAEngine
{
  namespace
  {
    bool HasDynamicCollider(const entt::registry& registry, entt::entity entity)
    {
      const auto* collider = registry.try_get<ColliderComponent>(entity);
      if (collider != nullptr && !collider->isStatic)
        return true;

      const auto* instanced = registry.try_get<InstancedColliderComponent>(entity);
      return instanced != nullptr && !instanced->isStatic;
    }

    entt::entity GetParent(const entt::registry& registry, entt::entity entity)
    {
      const auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
      return hierarchy != nullptr ? hierarchy->parent : entt::entity(entt::null);
    }

    bool IsOverride(BakeInclusionReason reason)
    {
      return reason == BakeInclusionReason::IncludeOverride
        || reason == BakeInclusionReason::ExcludeOverride;
    }

    // One hierarchy step of the rule: what an entity resolves to given its parent's answer.
    BakeInclusion Inherit(const entt::registry& registry, entt::entity entity,
      const BakeInclusion& parent)
    {
      const auto* bakeOverride = registry.try_get<BakeOverrideComponent>(entity);
      if (bakeOverride != nullptr && bakeOverride->mode != BakeOverride::Auto)
      {
        const bool exclude = bakeOverride->mode == BakeOverride::Exclude;
        return BakeInclusion {
          .excluded = exclude,
          .reason = exclude ? BakeInclusionReason::ExcludeOverride : BakeInclusionReason::IncludeOverride,
          .source = entity,
        };
      }

      // An override further up outranks a collider down here: the nearest explicit
      // decision is the one that counts, and the collider rule is only the fallback.
      if (!IsOverride(parent.reason) && HasDynamicCollider(registry, entity))
      {
        return BakeInclusion {
          .excluded = true,
          .reason = BakeInclusionReason::DynamicCollider,
          .source = entity,
        };
      }

      return parent;
    }
  }

  BakeExclusionResolver::BakeExclusionResolver(const entt::registry& registry)
    : m_Registry(registry)
  {
  }

  BakeInclusion ResolveBakeInclusion(const entt::registry& registry, entt::entity entity)
  {
    if (entity == entt::null || !registry.valid(entity))
      return BakeInclusion {};

    // Recursive so the root is decided first, as the resolver's chain does, with the call
    // stack standing in for the chain container.
    return Inherit(registry, entity, ResolveBakeInclusion(registry, GetParent(registry, entity)));
  }

  BakeInclusion BakeExclusionResolver::Resolve(entt::entity entity)
  {
    if (auto it = m_Memo.find(entity); it != m_Memo.end())
      return it->second;

    // Up to the first ancestor already resolved, then back down: every entity is decided
    // from its parent's answer, which is what lets the memo cover whole subtrees.
    m_Chain.clear();
    BakeInclusion inherited;
    for (entt::entity current = entity;
         current != entt::null && m_Registry.valid(current);
         current = GetParent(m_Registry, current))
    {
      if (auto it = m_Memo.find(current); it != m_Memo.end())
      {
        inherited = it->second;
        break;
      }
      m_Chain.push_back(current);
    }

    for (auto it = m_Chain.rbegin(); it != m_Chain.rend(); ++it)
    {
      inherited = Inherit(m_Registry, *it, inherited);
      m_Memo[*it] = inherited;
    }

    return inherited;
  }
}

#endif
